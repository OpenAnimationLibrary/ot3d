#pragma once

#include <functional>
#include <memory>

class QString;
class QWidget;
namespace otam {
struct Model;
}

void editAMDistortionCage(QWidget *parent, const QString &state,
                          const std::shared_ptr<const otam::Model> &model,
                          int frame,
                          const std::function<void(const QString &)> &apply);
