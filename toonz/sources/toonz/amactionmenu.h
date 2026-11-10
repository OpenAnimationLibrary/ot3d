#pragma once

class QMenu;
class TXsheet;

void addAMActionMenu(QMenu &menu, TXsheet *xsheet, int row, int column);

class TXshLevelColumn;
struct AMActionBinding;
class QString;

void changeAMActionBinding(TXshLevelColumn *column, AMActionBinding binding,
                           const QString &name);

class QWidget;
void reloadAMActionFromFile(TXshLevelColumn *column, QWidget *parent);
