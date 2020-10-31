// SPDX-License-Identifier: GPL-2.0
// Types used for the statistic widgets. There are three kinds of types:
// 1) Discrete types can only adopt discrete values.
//    Examples are dive-type or dive buddy.
//    Note that for example dive buddy means that a dive can have
//    multiple values.
// 2) Continuous types have a notion of a linear distance and can be
//    plotted on a linear axis.
//    An Example is the dive-date.
// 3) Numerical types are continuous types that support operations
//    such as averaging.
// Not every type makes sense in every type of graph.
#ifndef STATS_TYPES_H
#define STATS_TYPES_H

#include <vector>
#include <memory>
#include <QString>
#include <QObject>

struct dive;

struct StatsBin {
	virtual ~StatsBin();
	virtual QString format() const = 0;
	virtual bool operator<(StatsBin &) const = 0;
	virtual bool operator==(StatsBin &) const = 0;
	bool operator!=(StatsBin &b) const { return !(*this == b); }
};

using StatsBinPtr = std::unique_ptr<StatsBin>;

struct StatsBinDives {
	StatsBinPtr bin;
	std::vector<dive *> dives;
};

struct StatsBinCount {
	StatsBinPtr bin;
	int count;
};

struct StatsBinner {
	virtual ~StatsBinner();
	virtual QString name() const; // Only needed if there are multiple binners for a type
	virtual std::vector<StatsBinDives> bin_dives(const std::vector<dive *> &dives) const = 0;
	virtual std::vector<StatsBinCount> count_dives(const std::vector<dive *> &dives) const = 0;
};

struct StatsType {
	enum class Type {
		Discrete,
		Continuous,
		Numeric
	};

	virtual ~StatsType();
	virtual Type type() const = 0;
	virtual QString name() const = 0;
	virtual std::vector<const StatsBinner *> binners() const = 0; // Note: may depend on current locale!
	const StatsBinner *getBinner(int idx) const; // Handles out of bounds gracefully (returns first binner)
};

extern const std::vector<const StatsType *> stats_types;

// Dummy object for our translations
class StatsTranslations : public QObject
{
	Q_OBJECT
};

#endif
