#pragma once

#ifndef CURVEIO_H
#define CURVEIO_H

#include <string>

class TDoubleParam;

class TFilePath;
class QString;

TFilePath saveCurve(TDoubleParam *curve);
void loadCurve(TDoubleParam *curve);
void exportCurve(TDoubleParam *curve, const std::string &name);

#endif

class QWidget;

QString chooseCurveFileToOpen(QWidget *parent);
bool readCurveFile(const QString &path, TDoubleParam *curve);
bool applyCurveFile(TDoubleParam *target, TDoubleParam *source);
void reloadCurveData(TDoubleParam *target, const TDoubleParam &source);
