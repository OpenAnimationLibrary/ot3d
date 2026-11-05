// SPDX-License-Identifier: BSD-3-Clause
// Local CLI fixture for the native panel test. It has no network code.
#include "../../glb/tests/glbfixture.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <cstdio>

QString scalar(const QString &script, const QString &key) {
  int i = script.lastIndexOf(key + "=\"");
  if (i < 0) return {};
  i += key.size() + 2;
  QString value;
  while (i < script.size()) {
    QChar c = script[i++];
    if (c == '"') return value;
    if (c == '\\' && i < script.size()) c = script[i++];
    value += c;
  }
  return {};
}

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  if (app.arguments().contains("--schema")) {
    QJsonArray schema;
    for (const auto &name : {"text_to_3d", "image_to_3d", "export_glb"}) {
      QJsonObject properties;
      if (QString(name) != "export_glb") {
        properties.insert("service", QJsonObject{{"type", "string"}});
        properties.insert("model", QJsonObject{{"type", "string"}});
        properties.insert("remesh", QJsonObject{{"type", "boolean"}});
      }
      properties.insert(QString(name) == "text_to_3d" ? "prompt" : "path",
                        QJsonObject{{"type", "string"}});
      schema.append(QJsonObject{
          {"name", name},
          {"input_schema", QJsonObject{{"properties", properties}}}});
    }
    const auto bytes = QJsonDocument(schema).toJson(QJsonDocument::Compact);
    std::fwrite(bytes.constData(), 1, bytes.size(), stdout);
    return 0;
  }
  QFile input;
  if (!input.open(stdin, QIODevice::ReadOnly)) return 1;
  const auto script = QString::fromUtf8(input.readAll());
  const auto prompt = scalar(script, "prompt");
  if (prompt == "wait") QThread::sleep(30);
  if (prompt == "quota") {
    std::puts("GPU quota exhausted");
    return 2;
  }
  const auto bytes = triangleGlb(0, true);
  QFile output(scalar(script, "export_glb path"));
  return output.open(QIODevice::WriteOnly) &&
                 output.write(reinterpret_cast<const char *>(bytes.data()),
                              bytes.size()) == qint64(bytes.size())
             ? 0
             : 1;
}
