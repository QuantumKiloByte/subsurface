// SPDX-License-Identifier: GPL-2.0
#ifndef TAB_DIVE_COMPUTER_H
#define TAB_DIVE_COMPUTER_H

#include "TabBase.h"
#include "ui_TabDiveComputer.h"
#include "qt-models/divecomputermodel.h"

class TabDiveComputer : public TabBase {
	Q_OBJECT
public:
	TabDiveComputer(QWidget *parent = 0);
	void updateData() override;
	void clear() override;
public slots:
	void tableClicked(const QModelIndex &index);
private:
	Ui::TabDiveComputer ui;
	DiveComputerModel model;
	DiveComputerSortedModel sortedModel;
};

#endif
