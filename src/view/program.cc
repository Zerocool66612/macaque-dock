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

#include "program.h"

#include <iostream>

#include <QDir>
#include <QGuiApplication>
#include <QFileDialog>
#include <QMessageBox>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QTimer>

#include "display/window_system.h"

#include "dock_panel.h"
#include <utils/draw_utils.h>

namespace crystaldock {

Program::Program(DockPanel* parent, MultiDockModel* model, const QString& appId,
                 const QString& label, Qt::Orientation orientation, const QPixmap& icon,
                 int minSize, int maxSize, const QString& command, bool isAppMenuEntry,
                 bool pinned)
    : IconBasedDockItem(parent, model, label, orientation, icon, minSize, maxSize),
      appId_(appId),
      appLabel_(label),
      command_(command),
      isAppMenuEntry_(isAppMenuEntry),
      pinned_(pinned),
      demandsAttention_(false),
      attentionStrong_(false),
      launching_(false) {
  init();
}

Program::Program(DockPanel* parent, MultiDockModel* model, const QString& appId,
                 const QString& label, Qt::Orientation orientation, const QPixmap& icon,
                 int minSize, int maxSize)
    : IconBasedDockItem(parent, model, label, orientation, icon, minSize, maxSize),
      appId_(appId),
      appLabel_(label),
      command_(""),
      isAppMenuEntry_(false),
      pinned_(false),
      demandsAttention_(false),
      attentionStrong_(false),
      launching_(false) {
  init();
}

void Program::init() {
  createMenu();
  animationTimer_.setInterval(500);
  connect(&animationTimer_, &QTimer::timeout, this, [this]() {
    attentionStrong_ = !attentionStrong_;
    parent_->update();
  });
  bounceTimer_.setInterval(kBounceIntervalMs);
  connect(&bounceTimer_, &QTimer::timeout, this, &Program::updateBounceAnimation);
  connect(&menu_, &QMenu::aboutToHide, this,
          [this]() {
            parent_->setShowingPopup(false);
          });
}

void Program::draw(QPainter *painter) const {
  painter->save();
  painter->setRenderHint(QPainter::Antialiasing);
  auto taskCount = static_cast<int>(tasks_.size());
  // For launching feedback if bouncing launcher icon is not enabled.
  if (taskCount == 0 && launching_&& !model_->bouncingLauncherIcon()) { taskCount = 1; }
  if (parent_->showTaskManager() && taskCount > 0) {
    // Macaque Dock running-window indicators.
    // One dot per open window, up to four dots.

    const int visibleDots =
        qMin(taskCount, 4);

    const bool isActive =
        getActiveTask() >= 0 ||
        attentionStrong_ ||
        (launching_ && !model_->bouncingLauncherIcon());

    const QColor indicatorColor =
        isActive
            ? model_->activeIndicatorColor2D()
            : model_->inactiveIndicatorColor2D();

    painter->setPen(Qt::NoPen);
    painter->setBrush(indicatorColor);

    const int dotSize = 5;
    const int dotSpacing = 3;

    if (isHorizontal()) {
      const int totalWidth =
          visibleDots * dotSize +
          (visibleDots - 1) * dotSpacing;

      int x =
          left_ +
          (getWidth() - totalWidth) / 2;

      int y;

      if (parent_->isTop()) {
        y = top_ - 7;
      } else {
        y = top_ + getHeight() + 3;
      }

      for (int i = 0; i < visibleDots; ++i) {
        painter->drawEllipse(
            QRectF(x, y, dotSize, dotSize));

        x += dotSize + dotSpacing;
      }

    } else {
      const int totalHeight =
          visibleDots * dotSize +
          (visibleDots - 1) * dotSpacing;

      int x;

      if (parent_->isLeft()) {
        x = left_ - 7;
      } else {
        x = left_ + getWidth() + 3;
      }

      int y =
          top_ +
          (getHeight() - totalHeight) / 2;

      for (int i = 0; i < visibleDots; ++i) {
        painter->drawEllipse(
            QRectF(x, y, dotSize, dotSize));

        y += dotSize + dotSpacing;
      }
    }
  }

  painter->setRenderHint(QPainter::Antialiasing, false);
  painter->restore();

  painter->save();
  if (bouncing_) {
    float bounceOffset = getBounceOffset();
    if (isHorizontal()) {
      painter->translate(0, bounceOffset);
    } else {
      painter->translate(bounceOffset, 0);
    }
  }

  IconBasedDockItem::draw(painter);
  if (!model_->groupTasksByApplication() && !tasks_.empty() &&
      parent_->itemCount(appId_) > 1) {
    QString letter;
    for (auto i = 0; i < label_.size(); ++i) {
      if (label_.at(i).isLetter()) {
        letter = label_.at(i).toUpper();
        break;
      }
    }
    QFont font;
    font.setPixelSize(getHeight() / 2);
    painter->setFont(font);
    drawBorderedText(left_ + getWidth() * 5 / 8, top_ + getHeight()  * 3 / 8,
                     getWidth() / 2, getHeight() * 5 / 8,
                     0, letter, 2, Qt::black, Qt::white, painter);
  }
  painter->restore();
}

void Program::mousePressEvent(QMouseEvent* e) {
  if (e->button() == Qt::LeftButton) { // Run the application.
    if (appId_ == kLockScreenId) {
      parent_->leaveEvent(nullptr);
      QTimer::singleShot(DockPanel::kExecutionDelayMs, [this]() {
        launch();
      });
    } else if (appId_ == kShowDesktopId) {
      WindowSystem::setShowingDesktop(!WindowSystem::showingDesktop());
    } else {
      if (tasks_.empty()) {
        launch();
        startBounceAnimation();
      } else {
        const auto mod = QGuiApplication::keyboardModifiers();
        if (mod & Qt::ShiftModifier) {
          launch();
          startBounceAnimation();
        } else {
          cycleThroughTasks(!(mod & Qt::ControlModifier));
        }
      }
    }
  } else if (e->button() == Qt::RightButton) {
    showPopupMenu(&menu_);
  } else if (e->button() == Qt::MiddleButton) {
    launch();
    startBounceAnimation();
  }
}

void Program::wheelEvent(QWheelEvent* e) {
  const int delta = e->angleDelta().y();
  if (delta == 0) {
    return;
  }
  cycleThroughTasks(delta < 0);
}

void Program::cycleThroughTasks(bool forward) {
  if (tasks_.empty()) {
    return;
  }

  const auto activeTask = getActiveTask();
  const auto lastTask = static_cast<int>(tasks_.size() - 1);
  if (activeTask >= 0) {
    // Cycles through tasks.
    int nextTask;
    if (forward) {
      nextTask = (activeTask < lastTask) ? (activeTask + 1) : -1;
    } else {
      nextTask = activeTask > 0 ? (activeTask - 1) : -1;
    }
    if (nextTask >= 0 && nextTask <= lastTask) {
      WindowSystem::activateWindow(tasks_[nextTask].window);
    } else {
      for (int i = 0; i <= lastTask; ++i) {
        WindowSystem::minimizeWindow(tasks_[i].window);
      }
    }
  } else {
    const auto nextTask = forward ? 0 : lastTask;
    WindowSystem::activateWindow(tasks_[nextTask].window);
  }
}

QString Program::getLabel() const {
  const unsigned taskCount = tasks_.size();
  return (taskCount > 1) ?
      label_ + " (" + QString::number(tasks_.size()) + " windows)" :
      label_;
}

bool Program::addTask(const WindowInfo* task) {
  if (!model_->groupTasksByApplication() && !tasks_.empty()) {
    return false;
  }

  auto* app = model_->findApplication(task->appId);
  if ((app && app->appId == appId_) || task->appId == appId_.toStdString()) {
    tasks_.push_back(ProgramTask(task->window, QString::fromStdString(task->title),
                                 task->demandsAttention));

    if (appId_.contains("Unity", Qt::CaseInsensitive)) {
      qDebug() << "UNITY TASK ACCEPTED:"
               << QString::fromStdString(task->title)
               << "TOTAL =" << tasks_.size();
    }
    if (task->demandsAttention) {
      setDemandsAttention(true);
    }
    updateMenu();
    if (!model_->groupTasksByApplication()) {
      setLabel(QString::fromStdString(task->title));
    }
    return true;
  }
  return false;
}

bool Program::updateTask(const WindowInfo* task) {
  if (task->appId != appId_.toStdString()) {
    return false;
  }

  for (auto& existingTask : tasks_) {
    if (existingTask.window == task->window) {
      existingTask.demandsAttention = task->demandsAttention;
      updateDemandsAttention();
      return true;
    }
  }

  return false;
}

bool Program::removeTask(void* window) {
  for (int i = 0; i < static_cast<int>(tasks_.size()); ++i) {
    if (tasks_[i].window == window) {
      tasks_.erase(tasks_.begin() + i);
      updateMenu();
      return true;
    }
  }
  return false;
}

bool Program::hasTask(void* window) {
  for (const auto& task : tasks_) {
    if (task.window == window) {
      return true;
    }
  }
  return false;
}

bool Program::beforeTask(const QString& program) {
  return (pinned_ && appLabel_ != program) || appLabel_ < program;
}

bool Program::shouldBeRemoved() {
  if (!tasks_.empty()) {
    return false;
  }
  if (model_->groupTasksByApplication()) {
    return !pinned_;
  } else {
    return !pinned_ || parent_->itemCount(appId_) > 1;
  }
}

void Program::launch() {
  launching_ = true;
  parent_->update();
  launch(command_);
  QTimer::singleShot(kLaunchingAcknowledgementDurationMs,
                     [this] {
                       launching_ = false; parent_->update();
                     });
}

void Program::pinUnpin() {
  pinned_ = !pinned_;
  if (pinned_) {
    model_->addLauncher(parent_->dockId(), LauncherConfig(appId_, label_, iconName_, command_));
  } else {  // !pinned
    model_->removeLauncher(parent_->dockId(), appId_);
    if (shouldBeRemoved()) {
      parent_->delayedRefresh();
    }
  }
  parent_->updatePinnedStatus(appId_, pinned_);
}

void Program::launch(const QString& command) {
  QStringList list = QProcess::splitCommand(command);
  QProcess process;
  process.setProgram(list.at(0));
  process.setArguments(list.mid(1));
  auto env = QProcessEnvironment::systemEnvironment();
  // Unset XDG_ACTIVATION_TOKEN.
  env.insert("XDG_ACTIVATION_TOKEN", "");
  // Unset layer-shell env.
  env.insert("QT_WAYLAND_SHELL_INTEGRATION", "");
  process.setProcessEnvironment(env);
  process.setWorkingDirectory(QDir::homePath());
  if (!process.startDetached()) {
    QMessageBox warning(QMessageBox::Warning, "Error",
                        QString("Could not run command: ") + command,
                        QMessageBox::Ok, nullptr, Qt::Tool);
    warning.exec();
  }
}

void Program::closeAllWindows() {
  for (const auto& task : tasks_) {
    WindowSystem::closeWindow(task.window);
  }
}

void Program::createMenu() {
  menu_.addSection(QIcon::fromTheme(iconName_), label_);

  if (isAppMenuEntry_ || pinned_) {
    pinAction_ = menu_.addAction(
        QString("Pinned"), this,
        [this] {
          pinUnpin();
        });
    pinAction_->setCheckable(true);
    pinAction_->setChecked(pinned_);
  }

  if (isAppMenuEntry_) {
    menu_.addAction(QIcon::fromTheme("list-add"), QString("&New Window"), this,
                    [this] { launch(); });
  }

  closeAction_ = menu_.addAction(QIcon::fromTheme("window-close"), QString("&Close Window"), this,
                                 [this] {
                                   parent_->minimize();
                                   QTimer::singleShot(DockPanel::kExecutionDelayMs, [this]{
                                     closeAllWindows();
                                   });
                                 });

  menu_.addSeparator();

  if (pinned_) {
    menu_.addAction(
        QIcon::fromTheme("preferences-desktop-icons"),
        QString("Change Dock &Icon..."),
        this,
        [this] {
          parent_->minimize();

          QTimer::singleShot(DockPanel::kExecutionDelayMs, [this] {
            const QString file = QFileDialog::getOpenFileName(
                parent_,
                QString("Choose Dock Icon"),
                QDir::homePath(),
                QString("Images (*.png *.svg *.svgz *.xpm *.jpg *.jpeg *.webp);;All Files (*)"));

            if (!file.isEmpty()) {
              model_->setCustomLauncherIcon(appId_, file);
              parent_->reloadTasks();
            }
          });
        });

    menu_.addAction(
        QIcon::fromTheme("edit-undo"),
        QString("Use &System Icon"),
        this,
        [this] {
          model_->setCustomLauncherIcon(appId_, QString());
          parent_->reloadTasks();
        });
  }

  menu_.addSeparator();
  menu_.addAction(QIcon::fromTheme("configure"), QString("Edit &Launchers"), parent_,
                  [this] {
                    parent_->minimize();
                    QTimer::singleShot(DockPanel::kExecutionDelayMs, [this]{
                      parent_->showEditLaunchersDialog();
                    });
                  });

  if (model_->showTaskManager(parent_->dockId())) {
    menu_.addAction(QIcon::fromTheme("configure"),
                    QString("Task Manager &Settings"),
                    parent_,
                    [this] {
                      parent_->minimize();
                      QTimer::singleShot(DockPanel::kExecutionDelayMs, [this]{
                        parent_->showTaskManagerSettingsDialog();
                      });
                    });
  }

  menu_.addSeparator();
  parent_->addPanelSettings(&menu_);

  updateMenu();
}

void Program::setDemandsAttention(bool value) {
  if (demandsAttention_ == value) {
    return;
  }

  demandsAttention_ = value;
  if (demandsAttention_) {
    animationTimer_.start();
  } else if (animationTimer_.isActive()) {
    animationTimer_.stop();
    attentionStrong_ = false;
  }
  parent_->update();
}

void Program::updateDemandsAttention() {
  for (const auto& task : tasks_) {
    if (task.demandsAttention) {
      setDemandsAttention(true);
      return;
    }
  }
  setDemandsAttention(false);
}

void Program::updateMenu() {
  closeAction_->setVisible(!tasks_.empty());
  closeAction_->setText(tasks_.size() > 1 ? "&Close All Windows" : "&Close Window");
}

void Program::startBounceAnimation() {
  if (!model_->bouncingLauncherIcon()) {
    return;
  }

  if (!bouncing_) {
    bouncing_ = true;
    bouncingUp_ = true;
    bounceProgress_ = 0.0f;
    setAnimationStartAsCurrent();
    bounceTimer_.start();
  }
}

void Program::updateBounceAnimation() {
  if (!bouncing_) {
    return;
  }

  float bounceStep = 1.0f / kBounceSteps;
  float nextBounceRatio = bounceProgress_ + bounceStep;
  if (nextBounceRatio < 1.0f) {
    bounceProgress_ = nextBounceRatio;
  } else {
    if (!bouncingUp_) {
      // Done and done
      bounceProgress_ = 1.0f;
      bouncing_ = false;
      bounceTimer_.stop();
      return;
    }

    // It was bouncing up
    bounceProgress_ = 0.0f;
    bouncingUp_ = false;
  }

  parent_->update();
}

float Program::getBounceOffset() const {
  float bounceOffset;
  if (bouncingUp_) {
    float ratio = 1.0f - std::pow(1.0f - bounceProgress_, kBounceEaseOut);
    bounceOffset = -kBounceHeight * ratio;
  } else {
    float ratio = std::pow(bounceProgress_, kBounceEaseIn);
    bounceOffset = -kBounceHeight * (1.0f - ratio);
  }

  return bounceOffset;
}

}  // namespace crystaldock
