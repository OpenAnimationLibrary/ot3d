#pragma once

class QMenu;
class TXsheet;
class TXshLevelColumn;
class QWidget;

void addVectorActionMenu(QMenu &menu, TXsheet *xsheet, int row, int column);
void reloadVectorActionFromFile(TXshLevelColumn *column, QWidget *parent);
