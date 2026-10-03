/*
Copyright (c) 2012-2020 Maarten Baert <maarten-baert@hotmail.com>

This file is part of SimpleScreenRecorder.

SimpleScreenRecorder is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

SimpleScreenRecorder is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with SimpleScreenRecorder.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "MainWindow.h"

#include "Logger.h"
#include "CommandLineOptions.h"
#include "Icons.h"
#include "Dialogs.h"
#include "EnumStrings.h"
#include "NVidia.h"
#include "PageWelcome.h"
#include "PageInput.h"
#include "PageOutput.h"
#include "PageRecord.h"
#include "PageDone.h"
#include <QMenu>
#include <QAction>
#include <QMenuBar>
#include <QDialog>
#include <QTabWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QStringList>
#include <QMetaProperty>

ENUMSTRINGS(MainWindow::enum_nvidia_disable_flipping) = {
	{MainWindow::NVIDIA_DISABLE_FLIPPING_ASK, "ask"},
	{MainWindow::NVIDIA_DISABLE_FLIPPING_YES, "yes"},
	{MainWindow::NVIDIA_DISABLE_FLIPPING_NO, "no"},
};

const QString MainWindow::WINDOW_CAPTION = "SimpleScreenRecorder";

MainWindow::MainWindow()
	: QMainWindow() {

	m_nvidia_reenable_flipping = false;
	m_old_geometry = QRect();

	setWindowTitle(WINDOW_CAPTION);
	setWindowIcon(g_icon_ssr);

	QWidget *centralwidget = new QWidget(this);
	setCentralWidget(centralwidget);

	m_page_welcome = new PageWelcome(this);
	// keep input/output instances for settings, but don't add them as wizard pages
	m_page_input = new PageInput(this);
	m_page_output = new PageOutput(this);
	m_page_record = new PageRecord(this);
	// PageDone is no longer part of the wizard flow
	m_page_done = new PageDone(this);

	m_stacked_layout = new QStackedLayout(centralwidget);
	// Only the welcome and record pages remain in the wizard
	m_stacked_layout->addWidget(m_page_welcome);
	m_stacked_layout->addWidget(m_page_record);

	// 'Settings' menu removed per user request

	LoadSettings();

	GoPageStart();

	QShortcut *shortcut = new QShortcut(QKeySequence::Close, this);
	connect(shortcut, SIGNAL(activated()), this, SLOT(close()));

	// warning for non-X11 window systems (e.g. Wayland)
	if(!IsPlatformX11()) {
		MessageBox(QMessageBox::Warning, NULL, MainWindow::WINDOW_CAPTION,
				   MainWindow::tr("You are using a non-X11 window system (e.g. Wayland) which is only partially supported by SimpleScreenRecorder. "
								  "Several features will most likely not work properly, consider choosing a X11/Xorg session at the login screen if you experience issues. "
								  "SimpleScreenRecorder is able to record Wayland sessions using the PipeWire backend, provided that your Wayland compositor supports it."),
				   BUTTON_OK, BUTTON_OK);
	}

	// warning for glitch with proprietary NVIDIA drivers
	if(GetNVidiaDisableFlipping() == NVIDIA_DISABLE_FLIPPING_ASK || GetNVidiaDisableFlipping() == NVIDIA_DISABLE_FLIPPING_YES) {
		if(NVidiaGetFlipping()) {
			bool disable;
			if(GetNVidiaDisableFlipping() == NVIDIA_DISABLE_FLIPPING_ASK) {
				enum_button button = MessageBox(QMessageBox::Warning, NULL, MainWindow::WINDOW_CAPTION,
												MainWindow::tr("SimpleScreenRecorder has detected that you are using the proprietary NVIDIA driver with flipping enabled. "
															   "This is known to cause glitches during recording. It is recommended to disable flipping. Do you want me to do this for you?\n\n"
															   "You can also change this option manually in the NVIDIA control panel.", "Don't translate 'flipping' unless NVIDIA does the same"),
												BUTTON_YES | BUTTON_YES_ALWAYS | BUTTON_NO | BUTTON_NO_NEVER, BUTTON_YES);
				if(button == BUTTON_YES_ALWAYS)
					SetNVidiaDisableFlipping(NVIDIA_DISABLE_FLIPPING_YES);
				if(button == BUTTON_NO_NEVER)
					SetNVidiaDisableFlipping(NVIDIA_DISABLE_FLIPPING_NO);
				disable = (button == BUTTON_YES || button == BUTTON_YES_ALWAYS);
			} else {
				disable = true;
			}
			if(disable) {
				if(NVidiaSetFlipping(false)) {
					m_nvidia_reenable_flipping = true;
				} else {
					SetNVidiaDisableFlipping(NVIDIA_DISABLE_FLIPPING_ASK);
					MessageBox(QMessageBox::Warning, NULL, MainWindow::WINDOW_CAPTION,
							   MainWindow::tr("I couldn't disable flipping for some reason - sorry! Try disabling it in the NVIDIA control panel.",
											  "Don't translate 'flipping' unless NVIDIA does the same"),
							   BUTTON_OK, BUTTON_OK);
				}
			}
		}
	}

	// change minimum size based on screen resolution
	QSize preferred_size = minimumSizeHint() + QSize(style()->pixelMetric(QStyle::PM_ScrollBarExtent), 0);
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
	QSize available_size(0, 0);
	for(QScreen *screen : QApplication::screens()) {
		QSize size = screen->availableGeometry().size() - QSize(80, 80);
		available_size = (available_size.isNull())? size : available_size.boundedTo(size);
	}
#else
	QSize available_size = QApplication::desktop()->availableGeometry().size() - QSize(80, 80);
#endif
	//qDebug() << preferred_size << available_size;
	if(!available_size.isNull())
		setMinimumSize(preferred_size.boundedTo(available_size));

	// show the window if needed
	if(!CommandLineOptions::GetStartHidden()) {
		show();
	}
	m_page_record->UpdateShowHide();

	// Hide any residual visible labels in this window matching these texts.
	auto hideMatchingTexts = [this](const QStringList& texts) {
		QList<QWidget*> widgets = this->findChildren<QWidget*>();
		for(QWidget* w : widgets) {
			// check for a 'text' property (QLabel, QPushButton, QGroupBox, etc.)
			QVariant v = w->property("text");
			if(v.isValid()) {
				QString t = v.toString();
				for(const QString& pattern : texts) {
					if(!pattern.isEmpty() && t.contains(pattern)) {
						w->hide();
						break;
					}
				}
			}
		}
	};

	hideMatchingTexts(QStringList() << "Settings" << "Aufnehmen");

	// start recording and/or activate schedule if needed
	if(CommandLineOptions::GetStartRecording()) {
		m_page_record->OnRecordStart();
	}
	if(CommandLineOptions::GetActivateSchedule()) {
		m_page_record->OnScheduleActivate();
	}

}

MainWindow::~MainWindow() {
	// nothing
}

void MainWindow::LoadSettings() {

	QSettings settings(CommandLineOptions::GetSettingsFile(), QSettings::IniFormat);

	SetNVidiaDisableFlipping(StringToEnum(settings.value("global/nvidia_disable_flipping", QString()).toString(), NVIDIA_DISABLE_FLIPPING_ASK));

	m_page_welcome->LoadSettings(&settings);
	m_page_input->LoadSettings(&settings);
	m_page_output->LoadSettings(&settings);
	m_page_record->LoadSettings(&settings);

}

void MainWindow::SaveSettings() {

	QSettings settings(CommandLineOptions::GetSettingsFile(), QSettings::IniFormat);
	settings.clear();

	settings.setValue("global/nvidia_disable_flipping", EnumToString(GetNVidiaDisableFlipping()));

	m_page_welcome->SaveSettings(&settings);
	m_page_input->SaveSettings(&settings);
	m_page_output->SaveSettings(&settings);
	m_page_record->SaveSettings(&settings);

}

bool MainWindow::IsBusy() {
	return (QApplication::activeModalWidget() != NULL || QApplication::activePopupWidget() != NULL);
}

bool MainWindow::Validate() {
	if(!m_page_input->Validate())
		return false;
	if(!m_page_output->Validate())
		return false;
	return true;
}

void MainWindow::Quit() {
	SaveSettings();
	if(m_nvidia_reenable_flipping) {
		NVidiaSetFlipping(true);
	}
	QApplication::quit();
}

void MainWindow::closeEvent(QCloseEvent* event) {
	if(m_page_record->ShouldBlockClose()) {
		event->ignore();
		return;
	}
	event->accept();
	Quit();
}

void MainWindow::GoPageStart() {
	if(m_page_welcome->GetSkipPage()) {
		m_stacked_layout->setCurrentWidget(m_page_record);
	} else {
		m_stacked_layout->setCurrentWidget(m_page_welcome);
	}
}
void MainWindow::GoPageWelcome() {
	m_stacked_layout->setCurrentWidget(m_page_welcome);
}
void MainWindow::GoPageInput() {
	// Input is now exposed through the Parameters dialog
	ShowParametersDialog();
}
void MainWindow::GoPageOutput() {
	// Output is now exposed through the Parameters dialog
	ShowParametersDialog();
}
void MainWindow::GoPageRecord() {
	m_stacked_layout->setCurrentWidget(m_page_record);
	m_page_record->StartPage();
}
void MainWindow::GoPageDone() {
	// Keep behavior: go to record page instead of a separate done page
	m_stacked_layout->setCurrentWidget(m_page_record);
}

void MainWindow::ShowParametersDialog() {
	if(IsBusy())
		return;
	// create a modal dialog that hosts the input/output pages in tabs
	QDialog dialog(this);
	dialog.setWindowTitle(tr("Parameters"));
	QVBoxLayout *vlayout = new QVBoxLayout(&dialog);
	QTabWidget *tabs = new QTabWidget(&dialog);

	// temporarily reparent the pages into the dialog
	QWidget *old_parent_input = m_page_input->parentWidget();
	QWidget *old_parent_output = m_page_output->parentWidget();

	tabs->addTab(m_page_input, tr("Input"));
	tabs->addTab(m_page_output, tr("Output"));

	vlayout->addWidget(tabs);

	QHBoxLayout *hlayout = new QHBoxLayout();
	hlayout->addStretch();
	QPushButton *btn_ok = new QPushButton(tr("OK"), &dialog);
	QPushButton *btn_cancel = new QPushButton(tr("Cancel"), &dialog);
	hlayout->addWidget(btn_ok);
	hlayout->addWidget(btn_cancel);
	vlayout->addLayout(hlayout);

	// When OK is clicked, if the Output tab is active ask for confirmation
	connect(btn_ok, &QPushButton::clicked, [&dialog, tabs]() {
		if(tabs->currentIndex() == 1) {
			QMessageBox::StandardButton reply = QMessageBox::question(&dialog, QObject::tr("Confirm"),
																	   QObject::tr("Apply changes to Output settings?"),
																	   QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
			if(reply == QMessageBox::Yes) {
				dialog.accept();
			}
		} else {
			dialog.accept();
		}
	});
	connect(btn_cancel, &QPushButton::clicked, &dialog, &QDialog::reject);

	int res = dialog.exec();

	// reparent pages back to the central widget to keep them available
	m_page_input->setParent(centralWidget());
	m_page_output->setParent(centralWidget());

	// ensure pages are hidden (they're not part of the stacked layout)
	m_page_input->hide();
	m_page_output->hide();

	if(res == QDialog::Accepted) {
		// apply/save settings if needed
		SaveSettings();
	} else {
		// reload settings to discard changes
		LoadSettings();
	}
}

void MainWindow::OnShow() {
	if(IsBusy())
		return;
	if(isVisible())
		return;
	show();
	if(!m_old_geometry.isNull()) {
		setGeometry(m_old_geometry);
		m_old_geometry = QRect();
	}
	m_page_record->UpdateShowHide();
}

void MainWindow::OnHide() {
	if(IsBusy())
		return;
	if(!isVisible())
		return;
	m_old_geometry = geometry();
	hide();
	m_page_record->UpdateShowHide();
}

void MainWindow::OnShowHide() {
	if(isVisible()) {
		OnHide();
	} else {
		OnShow();
	}
}

void MainWindow::OnSysTrayActivated(QSystemTrayIcon::ActivationReason reason) {
	if(reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
		OnShowHide();
	}
}
