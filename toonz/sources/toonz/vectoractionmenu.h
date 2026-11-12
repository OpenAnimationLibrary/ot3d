#pragma once

class QMenu;
class TXsheet;
class TXshLevelColumn;
class ToonzScene;
struct VectorActionBinding;
class QWidget;

void addVectorActionMenu(QMenu &menu, TXsheet *xsheet, int row, int column);
void reloadVectorActionFromFile(TXshLevelColumn *column, QWidget *parent);

bool vectorActionFileDiffers(const ToonzScene &scene,
                             const VectorActionBinding &binding);
