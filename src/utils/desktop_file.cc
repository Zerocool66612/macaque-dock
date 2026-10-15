/*
 * This file is part of Crystal Dock.
 * Copyright (C) 2022 Viet Dang (dangvd@gmail.com)
 *
 * Crystal Dock is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Crystal Dock is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Crystal Dock.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "desktop_file.h"

#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QTextStream>

namespace crystaldock {

DesktopFile::DesktopFile(const QString& file) {
  QFile inputFile(file);
  if (inputFile.open(QIODevice::ReadOnly)) {
    appId_ = QFileInfo(file).completeBaseName().toLower();
    QTextStream input(&inputFile);
    QString section;
    while (!input.atEnd()) {
      const QString line = input.readLine().trimmed();

      if (line.startsWith('[') && line.endsWith(']')) {
        section = line.mid(1, line.size() - 2);
        continue;
      }

      if (line.isEmpty() || line.startsWith('#')) {
        continue;
      }

      const int index = line.indexOf('=');
      if (index <= 0) {
        continue;
      }

      const QString key = line.left(index);
      const QString value = line.mid(index + 1);

      if (section == "Desktop Entry") {
        values_[key] = value;
      } else if (section.startsWith("Desktop Action ")) {
        actionValues_[section.mid(15)][key] = value;
      }
    }
  }
}

QList<DesktopFile::Action> DesktopFile::actions() const {
  QList<Action> result;

  const QStringList ids =
      values_.value("Actions").split(';', Qt::SkipEmptyParts);

  for (const QString& id : ids) {
    if (!actionValues_.contains(id)) {
      continue;
    }

    const auto& values = actionValues_[id];

    Action action;
    action.id = id;
    action.name = values.value("Name");
    action.exec = values.value("Exec");
    action.icon = values.value("Icon");

    if (!action.name.isEmpty() && !action.exec.isEmpty()) {
      result.append(action);
    }
  }

  return result;
}

bool DesktopFile::write(const QString &file) {
  QFile outputFile(file);
  if (outputFile.open(QIODevice::WriteOnly)) {
    QTextStream output(&outputFile);
    output << "[Desktop Entry]\n";
    for (const auto& key : values_.keys()) {
      output << key << "=" << values_[key] << "\n";
    }

    return true;
  }
  return false;
}

bool DesktopFile::showOnDesktop(const QString& desktop) const {
  if (!onlyShowIn().empty() && !onlyShowIn().contains(desktop)) {
    return false;
  }

  if (!notShowIn().empty() && notShowIn().contains(desktop)) {
    return false;
  }

  return true;
}

}  // namespace crystaldock
