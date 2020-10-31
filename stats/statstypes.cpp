// SPDX-License-Identifier: GPL-2.0
#include "statstypes.h"
#include "core/dive.h"
#include "core/divemode.h"
#include "core/pref.h"
#include "core/subsurface-time.h"

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
#define SKIP_EMPTY Qt::SkipEmptyParts
#else
#define SKIP_EMPTY QString::SkipEmptyParts
#endif

// Note: usually I dislike functions defined inside class/struct
// declarations ("Java style"). However, for brevity this is done
// in this rather template-heavy source file consistently.

// First, let's define the virtual destructors of our base classes
StatsBin::~StatsBin()
{
}

StatsBinner::~StatsBinner()
{
}

StatsType::~StatsType()
{
}

QString StatsBinner::name() const
{
	return QStringLiteral("N/D"); // Some dummy string that should never reach the UI
}

const StatsBinner *StatsType::getBinner(int idx) const
{
	std::vector<const StatsBinner *> b = binners();
	if (b.empty())
		return nullptr;
	return idx >= 0 && idx < (int)b.size() ? b[idx] : b[0];
}

// Silly template, which spares us defining type() member functions.
template<StatsType::Type t>
struct StatsTypeTemplate : public StatsType {
	Type type() const override { return t; }
};

// A simple bin that is based on copyable value and can be initialized from
// that value. This template spares us from writing one-line constructors.
template<typename Type>
struct SimpleBin : public StatsBin {
	Type value;
	SimpleBin(const Type &v) : value(v) { }

	// This must not be called for different types. It will crash with an exception.
	bool operator<(StatsBin &b) const {
		return value < dynamic_cast<SimpleBin &>(b).value;
	}

	bool operator==(StatsBin &b) const {
		return value == dynamic_cast<SimpleBin &>(b).value;
	}
};

// A string bin is s simple bin where the format is simply its value
struct StringBin : public SimpleBin<QString> {
	using SimpleBin::SimpleBin;
	QString format() const override {
		return value;
	}
};

// A general binner template that works on trivial bins that are based
// on a type that is equality and less-than comparable. The bins
// are supposed to feature a static to_bin_value() function that turns the dive
// into a value and to be constructable from said value.
template<typename BinType>
struct SimpleBinner : public StatsBinner {
public:
	using Type = decltype(BinType::to_bin_value(nullptr));
	std::vector<StatsBinDives> bin_dives(const std::vector<dive *> &dives) const override;
	std::vector<StatsBinCount> count_dives(const std::vector<dive *> &dives) const override;
};

// Wrapper around std::lower_bound that searches for a value in a
// vector of pairs. Comparison is made with the first element of the pair.
// std::lower_bound does a binary search and this is used to keep a
// vector in ascending order.
template<typename T1, typename T2>
auto pair_lower_bound(std::vector<std::pair<T1, T2>> &v, const T1 &value)
{
	return std::lower_bound(v.begin(), v.end(), value,
	       			[] (const std::pair<T1, T2> &entry, const T1 &value) {
					return entry.first < value;
				});
}

// Add a dive to a vector of (value, dive_list) pairs. If the value doesn't yet
// exist, create a new entry in the vector.
template<typename T>
using ValueDiveListPair = std::pair<T, std::vector<dive *>>;
template<typename T>
void add_dive_to_value_bin(std::vector<ValueDiveListPair<T>> &v, const T &value, dive *d)
{
	// Does that value already exist?
	auto it = pair_lower_bound(v, value);
	if (it != v.end() && it->first == value)
		it->second.push_back(d);	// Bin exists -> add dive!
	else
		v.insert(it, { value, { d }});	// Bin does not exist -> insert at proper location.
}

// Increase count in a vector of (value, count) pairs. If the value doesn't yet
// exist, create a new entry in the vector.
template<typename T>
using ValueCountPair = std::pair<T, int>;
template<typename T>
void increment_count_bin(std::vector<ValueCountPair<T>> &v, const T &value)
{
	// Does that value already exist?
	auto it = pair_lower_bound(v, value);
	if (it != v.end() && it->first == value)
		++it->second;			// Bin exists -> increment count!
	else
		v.insert(it, { value, 1 });	// Bin does not exist -> insert at proper location.
}

template<typename BinType>
std::vector<StatsBinDives> SimpleBinner<BinType>::bin_dives(const std::vector<dive *> &dives) const
{
	// First, collect a value / dives vector and then produce the final vector
	// out of that. I wonder if that is permature optimization?
	using Pair = ValueDiveListPair<Type>;
	std::vector<Pair> value_bins;
	for (dive *d: dives) {
		Type value = BinType::to_bin_value(d);
		add_dive_to_value_bin(value_bins, value, d);
	}

	// Now, turn that into our result array with allocated bin objects.
	std::vector<StatsBinDives> res;
	res.reserve(value_bins.size());
	for (const Pair &pair: value_bins)
		res.push_back({ std::make_unique<BinType>(pair.first), std::move(pair.second)});
	return res;
}

template<typename BinType>
std::vector<StatsBinCount> SimpleBinner<BinType>::count_dives(const std::vector<dive *> &dives) const
{
	// First, collect a value / counts vector and then produce the final vector
	// out of that. I wonder if that is permature optimization?
	using Pair = std::pair<Type, int>;
	std::vector<Pair> value_bins;
	for (const dive *d: dives) {
		Type value = BinType::to_bin_value(d);
		increment_count_bin(value_bins, value);
	}

	// Now, turn that into our result array with allocated bin objects.
	std::vector<StatsBinCount> res;
	res.reserve(value_bins.size());
	for (const Pair &pair: value_bins)
		res.push_back({ std::make_unique<BinType>(pair.first), pair.second});
	return res;
}

// A binner that works on string-based bins whereby each dive can
// produce multiple strings (e.g. dive buddies). The bins are supposed
// to feature a to_string_list() function that produces a vector
// of QStrings
template<typename BinType>
struct StringBinner : public StatsBinner {
public:
	std::vector<StatsBinDives> bin_dives(const std::vector<dive *> &dives) const override;
	std::vector<StatsBinCount> count_dives(const std::vector<dive *> &dives) const override;
};

template<typename BinType>
std::vector<StatsBinDives> StringBinner<BinType>::bin_dives(const std::vector<dive *> &dives) const
{
	// First, collect a value / dives vector and then produce the final vector
	// out of that. I wonder if that is permature optimization?
	using Pair = ValueDiveListPair<QString>;
	std::vector<Pair> value_bins;
	for (dive *d: dives) {
		for (const QString &s: BinType::to_string_list(d))
			add_dive_to_value_bin(value_bins, s, d);
	}

	// Now, turn that into our result array with allocated bin objects.
	std::vector<StatsBinDives> res;
	res.reserve(value_bins.size());
	for (const Pair &pair: value_bins)
		res.push_back({ std::make_unique<BinType>(pair.first), std::move(pair.second)});
	return res;
}

template<typename BinType>
std::vector<StatsBinCount> StringBinner<BinType>::count_dives(const std::vector<dive *> &dives) const
{
	// First, collect a value / counts vector and then produce the final vector
	// out of that. I wonder if that is permature optimization?
	using Pair = std::pair<QString, int>;
	std::vector<Pair> value_bins;
	for (const dive *d: dives) {
		for (const QString &s: BinType::to_string_list(d))
			increment_count_bin(value_bins, s);
	}

	// Now, turn that into our result array with allocated bin objects.
	std::vector<StatsBinCount> res;
	res.reserve(value_bins.size());
	for (const Pair &pair: value_bins)
		res.push_back({ std::make_unique<BinType>(pair.first), pair.second});
	return res;
}

// ============ The date of the dive by year, quarter or month ============
// (Note that calendar week is defined differently in different parts of the world and therefore omitted for now)

struct DateYearBin : public SimpleBin<int> {
	using SimpleBin::SimpleBin;
	QString format() const override {
		return QString::number(value);
	}
	static int to_bin_value(const dive *d) {
		return utc_year(d->when);
	}
};

struct DateYearBinner : public SimpleBinner<DateYearBin> {
	QString name() const override {
		return StatsTranslations::tr("Yearly");
	}
};

using year_quarter = std::pair<unsigned short, unsigned short>;
struct DateQuarterBin : public SimpleBin<year_quarter> {
	using SimpleBin::SimpleBin;
	QString format() const override {
		return StatsTranslations::tr("%1 Q%2").arg(QString::number(value.first),
							   QString::number(value.second));
	}
	static year_quarter to_bin_value(const dive *d) {
		struct tm tm;
		utc_mkdate(d->when, &tm);

		int year = tm.tm_year;
		switch (tm.tm_mon) {
		case 0 ... 2: return { year, 1 };
		case 3 ... 5: return { year, 2 };
		case 6 ... 8: return { year, 3 };
		default:      return { year, 4 };
		}
	}
};

struct DateQuarterBinner : public SimpleBinner<DateQuarterBin> {
	QString name() const override {
		return StatsTranslations::tr("Quarterly");
	}
};

using year_month = std::pair<unsigned short, unsigned short>;
struct DateMonthBin : public SimpleBin<year_month> {
	using SimpleBin::SimpleBin;
	QString format() const override {
		return StatsTranslations::tr("%1 %2").arg(QString(monthname(value.second)),
							  QString::number(value.first));
	}
	static year_month to_bin_value(const dive *d) {
		struct tm tm;
		utc_mkdate(d->when, &tm);
		return { tm.tm_year, tm.tm_mon };
	}
};

struct DateMonthBinner : public SimpleBinner<DateMonthBin> {
	QString name() const override {
		return StatsTranslations::tr("Monthly");
	}
};

static DateYearBinner date_year_binner;
static DateQuarterBinner date_quarter_binner;
static DateMonthBinner date_month_binner;
struct DateType : public StatsTypeTemplate<StatsType::Type::Discrete> {
	QString name() const {
		return StatsTranslations::tr("Date");
	}
	std::vector<const StatsBinner *> binners() const override {
		return { &date_year_binner, &date_quarter_binner, &date_month_binner };
	}
};

// ============ Dive depth, binned in 5, 10, 20 m or 15, 30, 60 ft bins ============

template <int BinSize>
struct MeterBin : public SimpleBin<int> {
	using SimpleBin::SimpleBin;
	QString format() const override {
		return StatsTranslations::tr("%1–%2 m").arg(QString::number(value * BinSize),
							    QString::number((value + 1) * BinSize));
	}
	static int to_bin_value(const dive *d) {
		return d->maxdepth.mm / 1000 / BinSize;
	}
};

template <int BinSize>
struct MeterBinner : public SimpleBinner<MeterBin<BinSize>> {
	QString name() const override {
		return StatsTranslations::tr("in %1 m steps").arg(BinSize);
	}
};

template <int BinSize>
struct FeetBin : public SimpleBin<int> {
	using SimpleBin::SimpleBin;
	QString format() const override {
		return StatsTranslations::tr("%1–%2 ft").arg(QString::number(value * BinSize),
							     QString::number((value + 1) * BinSize));
	}
	static int to_bin_value(const dive *d) {
		return lrint(mm_to_feet(d->maxdepth.mm)) / BinSize;
	}
};

template <int BinSize>
struct FeetBinner : public SimpleBinner<FeetBin<BinSize>> {
	QString name() const override {
		return StatsTranslations::tr("in %1 ft steps").arg(BinSize);
	}
};

static MeterBinner<5> meter_binner5;
static MeterBinner<10> meter_binner10;
static MeterBinner<20> meter_binner20;
static FeetBinner<15> feet_binner15;
static FeetBinner<30> feet_binner30;
static FeetBinner<60> feet_binner60;
struct DepthType : public StatsTypeTemplate<StatsType::Type::Numeric> {
	QString name() const override {
		return StatsTranslations::tr("Depth");
	}
	std::vector<const StatsBinner *> binners() const override {
		if (prefs.units.length == units::METERS)
			return { &meter_binner5, &meter_binner10, &meter_binner20 };
		else
			return { &feet_binner15, &feet_binner30, &feet_binner60 };
	}
};

// ============ Dive mode ============
struct DiveModeBin : public SimpleBin<int> {
	using SimpleBin::SimpleBin;
	QString format() const override {
		return QString(divemode_text_ui[value]);
	}
	static int to_bin_value(const dive *d) {
		int res = (int)d->dc.divemode;
		return res >= 0 && res < NUM_DIVEMODE ? res : OC;
	}
};

struct DiveModeBinner : public SimpleBinner<DiveModeBin> {
};

static DiveModeBinner dive_mode_binner;
struct DiveModeType : public StatsTypeTemplate<StatsType::Type::Discrete> {
	QString name() const override {
		return StatsTranslations::tr("Dive mode");
	}
	std::vector<const StatsBinner *> binners() const override {
		return { &dive_mode_binner };
	}
};

// ============ Buddy (including dive guides) ============
struct BuddyBin : public StringBin {
	using StringBin::StringBin;
	static std::vector<QString> to_string_list(const dive *d) {
		std::vector<QString> dive_people;
		for (const QString &s: QString(d->buddy).split(",", SKIP_EMPTY))
			dive_people.push_back(s.trimmed());
		for (const QString &s: QString(d->divemaster).split(",", SKIP_EMPTY))
			dive_people.push_back(s.trimmed());
		return dive_people;
	}
};

struct BuddyBinner : public StringBinner<BuddyBin> {
};

static BuddyBinner buddy_binner;
struct BuddyType : public StatsTypeTemplate<StatsType::Type::Discrete> {
	QString name() const override {
		return StatsTranslations::tr("Buddies");
	}
	std::vector<const StatsBinner *> binners() const override {
		return { &buddy_binner };
	}
};

static DateType date_type;
static DepthType depth_type;
static DiveModeType dive_mode_type;
static BuddyType buddy_type;
const std::vector<const StatsType *> stats_types = {
	&date_type, &depth_type, &dive_mode_type, &buddy_type
};
