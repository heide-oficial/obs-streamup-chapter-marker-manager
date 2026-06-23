#include "chapter-marker-dock.hpp"
#include "annotation-dock.hpp"
#include "constants.hpp"
#include "streamup-record-chapter-manager.hpp"
#include <streamup/ui/window-chrome.hpp> // ShadowDialog, applyChrome, RoundedContainer, DragFilter
#include <streamup/ui/switch-button.hpp> // SwitchButton, CreateStyledSwitch
#include <streamup/ui/pill-button.hpp>   // PillButton
#include <streamup/ui/mac-inputs.hpp>    // MacComboBox, MacSpinBox
#include <streamup/ui/labels.hpp>        // labelStyle, dimLabelStyle, formLabelStyle, sectionHeader
#include <streamup/ui/dialogs.hpp>       // confirm, prompt, info
#include <streamup/ui/ui-scrollbar.hpp>  // useScrollBars (capsule scrollbars)
#include "version.h"
#include <obs-data.h>
#include <obs-frontend-api.h>
#include <obs-module.h>
#include <QApplication>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QListWidget>
#include <QMainWindow>
#include <QMutex>
#include <QStyle>
#include <QTextStream>
#include <QVBoxLayout>

using namespace StreamUP::UIStyles;

// Labeled switch row — replaces the old SwitchWidget. The canonical SwitchButton
// renders its own inline label, so this is just construct + setText.
static SwitchButton *makeSwitch(const QString &text, QWidget *parent = nullptr)
{
	auto *s = new SwitchButton(parent);
	s->setText(text);
	return s;
}

// WARNING: QT_TO_UTF8 returns a pointer to a temporary QByteArray.
// Only safe when used immediately (e.g., passed directly to a function).
// Never store the returned pointer for later use.
#define QT_TO_UTF8(str) str.toUtf8().constData()

extern void AddChapterMarkerHotkey(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed);
extern QString currentChapterName;
extern QMutex currentChapterNameMutex;

static void DockFrontendEventCallback(obs_frontend_event event, void *ptr);

// Helper to detect NTSC drop-frame rates (29.97, 59.94)
static bool isNTSCDropFrame()
{
	obs_video_info ovi;
	if (obs_get_video_info(&ovi) && ovi.fps_den > 0) {
		double actualFps = static_cast<double>(ovi.fps_num) / ovi.fps_den;
		return (fabs(actualFps - 29.97) < 0.01) || (fabs(actualFps - 59.94) < 0.01);
	}
	return false;
}

// Helper to append chapter source info to a chapter name if enabled
static QString appendChapterSource(const QString &chapterName, const QString &chapterSource, bool enabled)
{
	if (enabled && !chapterName.endsWith(" (" + chapterSource + ")")) {
		return chapterName + " (" + chapterSource + ")";
	}
	return chapterName;
}

// Helper to escape XML special characters in strings written to FCPXML/Premiere XML
static QString escapeXml(const QString &str)
{
	QString escaped = str;
	escaped.replace("&", "&amp;");
	escaped.replace("<", "&lt;");
	escaped.replace(">", "&gt;");
	escaped.replace("\"", "&quot;");
	escaped.replace("'", "&apos;");
	return escaped;
}

//--------------------CONSTRUCTOR & DESTRUCTOR--------------------
ChapterMarkerDock::ChapterMarkerDock(QWidget *parent)
	: QFrame(parent),
	  annotationDock(nullptr),
	  exportChaptersToTextEnabled(false),
	  exportChaptersToFCPXMLEnabled(false),
	  exportChaptersToPremiereXMLEnabled(false),
	  exportChaptersToEDLEnabled(false),
	  exportChaptersToFileEnabled(false),
	  insertChapterMarkersInVideoEnabled(false),
	  exportTextFilePath(""),
	  exportFCPXMLFilePath(""),
	  exportPremiereXMLFilePath(""),
	  exportEDLFilePath(""),
	  defaultChapterName(obs_module_text("DefaultChapterName")),
	  edlEventNumber(1),
	  fcpMarkerID(1),
	  ignoredScenes(),
	  chapterOnSceneChangeEnabled(false),
	  showPreviousChaptersEnabled(false),
	  fullChapterHistoryEnabled(false),
	  addChapterSourceEnabled(false),
	  chapterCount(Constants::DEFAULT_CHAPTER_COUNT),
	  useIncrementalChapterNames(false),
	  settingsDialog(nullptr),
	  isFirstRunInRecording(true),
	  recordingStartFrameCount(0),
	  presetChapters(),
	  chapterHotkeys(),
	  presetChaptersDialog(nullptr),
	  presetChapterNameInput(nullptr),
	  addChapterButton(nullptr),
	  removeChapterButton(nullptr),
	  chaptersListWidget(nullptr),
	  exportChaptersToFileCheckbox(nullptr),
	  exportChaptersToTextCheckbox(nullptr),
	  exportChaptersToFCPXMLCheckbox(nullptr),
	  exportChaptersToPremiereXMLCheckbox(nullptr),
	  exportChaptersToEDLCheckbox(nullptr),
	  exportSettingsGroup(nullptr),
	  insertChapterMarkersCheckbox(nullptr),
	  ignoredScenesDialog(nullptr),
	  sceneChangeSettingsGroup(nullptr),
	  chapterNameInput(new QLineEdit(this)),
	  settingsButton(new QPushButton(this)),
	  setIgnoredScenesButton(nullptr),
	  annotationButton(new QPushButton(this)),
	  setPresetChaptersButton(nullptr),
	  currentChapterTextLabel(new QLabel(obs_module_text("CurrentChapterLabel"), this)),
	  currentChapterNameLabel(new QLabel(obs_module_text("RecordingNotActive"), this)),
	  feedbackLabel(new QLabel("", this)),
	  previousChaptersList(new QListWidget(this)),
	  previousChaptersGroup(nullptr),
	  saveChapterMarkerButton(new QPushButton(obs_module_text("SaveChapterMarkerButton"), this)),
	  defaultChapterNameEdit(nullptr),
	  showPreviousChaptersCheckbox(nullptr),
	  fullChapterHistoryCheckbox(nullptr),
	  addChapterSourceCheckbox(nullptr),
	  chapterOnSceneChangeCheckbox(nullptr),
	  ignoredScenesListWidget(nullptr),
	  chapters(),
	  timestamps(),
	  exportSubWidget(nullptr),
	  exportSettingsLayout(nullptr)
{
	// UI Setup
	setupMainDockUI();
	// Signal Connections
	setupConnections();
	// OBS Event Callback
	setupOBSCallbacks();
	// initialise UI States
	refreshMainDockUI();
}

ChapterMarkerDock::~ChapterMarkerDock()
{
	// Remove the OBS frontend event callback registered in setupOBSCallbacks
	obs_frontend_remove_event_callback(DockFrontendEventCallback, this);

	for (const auto &chapterName : chapterHotkeys.keys()) {
		unregisterChapterHotkey(chapterName);
	}

	// Qt parent-child relationship handles deletion of settingsDialog and
	// ignoredScenesDialog since they are parented to this widget.
	settingsDialog = nullptr;
	ignoredScenesDialog = nullptr;
}

//--------------------SIGNAL CONNECTIONS--------------------
void ChapterMarkerDock::setupConnections()
{
	// Websocket add annotation
	connect(this, &ChapterMarkerDock::addAnnotationSignal, this, &ChapterMarkerDock::onAddAnnotation);

	// Websocket add chapter
	connect(this, &ChapterMarkerDock::addChapterMarkerSignal, this, &ChapterMarkerDock::onAddChapterMarker);

	// Enter Chapter Name text field and button
	connect(chapterNameInput, &QLineEdit::returnPressed, saveChapterMarkerButton, &QPushButton::click);
	connect(saveChapterMarkerButton, &QPushButton::clicked, this, &ChapterMarkerDock::onAddChapterMarkerButton);

	// Settings button
	connect(settingsButton, &QPushButton::clicked, this, &ChapterMarkerDock::onSettingsClicked);

	// Annotation button
	connect(annotationButton, &QPushButton::clicked, this, &ChapterMarkerDock::onAnnotationClicked);

	// Previous Chapter list
	connect(previousChaptersList, &QListWidget::itemClicked, this, &ChapterMarkerDock::onPreviousChapterSelected);
	connect(previousChaptersList, &QListWidget::itemDoubleClicked, this, &ChapterMarkerDock::onPreviousChapterDoubleClicked);

	// Feedback label timer
	feedbackTimer.setInterval(Constants::FEEDBACK_TIMER_INTERVAL);
	feedbackTimer.setSingleShot(true);
	connect(&feedbackTimer, &QTimer::timeout, [this]() { feedbackLabel->setText(""); });
}

// TODO: OBS does not currently emit a frontend event when a recording file is split.
// When split recording support is added upstream, handle it here to reset export file
// paths and timestamps so chapters align with the new file.
static void DockFrontendEventCallback(obs_frontend_event event, void *ptr)
{
	auto dock = static_cast<ChapterMarkerDock *>(ptr);
	if (event == OBS_FRONTEND_EVENT_SCENE_CHANGED || event == OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED) {
		dock->onSceneChanged();
	} else if (event == OBS_FRONTEND_EVENT_RECORDING_STOPPED) {
		dock->onRecordingStopped();
	}
}

void ChapterMarkerDock::setupOBSCallbacks()
{
	obs_frontend_add_event_callback(DockFrontendEventCallback, this);
}

//--------------------MAIN DOCK UI--------------------
void ChapterMarkerDock::setupMainDockUI()
{
	// Set the frame style
	this->setFrameStyle(QFrame::StyledPanel | QFrame::Raised);

	// Set object name for theme targeting
	this->setObjectName("chapterMarkerDock");

	// Add padding to the QFrame itself
	this->setContentsMargins(S(8), S(8), S(8), S(8));

	QVBoxLayout *mainDockLayout = new QVBoxLayout(this);
	mainDockLayout->setObjectName("chapterMarkerDockLayout");
	mainDockLayout->setAlignment(Qt::AlignTop | Qt::AlignLeft);
	mainDockLayout->setSizeConstraint(QLayout::SetMinimumSize);
	mainDockLayout->setContentsMargins(0, 0, 0, 0);
	mainDockLayout->setSpacing(0);

	setupMainDockCurrentChapterLayout(mainDockLayout);
	mainDockLayout->addSpacing(S(6));
	setupMainDockChapterInput(mainDockLayout);
	mainDockLayout->addSpacing(S(6));
	setupMainDockSaveButtonLayout(mainDockLayout);
	mainDockLayout->addSpacing(S(6));
	setupMainDockFeedbackLabel(mainDockLayout);
	mainDockLayout->addSpacing(S(6));
	setupMainDockPreviousChaptersGroup(mainDockLayout);

	setLayout(mainDockLayout);
}

void ChapterMarkerDock::setupMainDockCurrentChapterLayout(QVBoxLayout *mainLayout)
{
	QHBoxLayout *chapterLabelLayout = new QHBoxLayout();
	chapterLabelLayout->setObjectName("chapterLabelLayout");
	chapterLabelLayout->setAlignment(Qt::AlignTop | Qt::AlignLeft);

	currentChapterTextLabel->setObjectName("currentChapterTextLabel");
	currentChapterTextLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
	currentChapterNameLabel->setObjectName("currentChapterNameLabel");
	currentChapterNameLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
	currentChapterNameLabel->setWordWrap(true);
	currentChapterNameLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	currentChapterNameLabel->setProperty("themeID", "error");

	chapterLabelLayout->addWidget(currentChapterTextLabel);
	chapterLabelLayout->addWidget(currentChapterNameLabel);

	mainLayout->addLayout(chapterLabelLayout);
}

void ChapterMarkerDock::setupMainDockChapterInput(QVBoxLayout *mainLayout)
{
	chapterNameInput->setObjectName("chapterNameInput");
	chapterNameInput->setPlaceholderText(obs_module_text("EnterChapterName"));
	chapterNameInput->setToolTip(obs_module_text("EnterChapterNameTooltip"));
	mainLayout->addWidget(chapterNameInput);
}

void ChapterMarkerDock::setupMainDockSaveButtonLayout(QVBoxLayout *mainLayout)
{
	QHBoxLayout *saveButtonLayout = new QHBoxLayout();
	saveButtonLayout->setObjectName("saveButtonLayout");

	// Make Save Chapter Marker button fill the horizontal space
	saveChapterMarkerButton->setObjectName("saveChapterMarkerButton");
	saveChapterMarkerButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	saveChapterMarkerButton->setToolTip(obs_module_text("SaveChapterMarkerButtonTooltip"));

	// Configure the settings button
	settingsButton->setObjectName("settingsButton");
	settingsButton->setToolTip(obs_module_text("SettingsTooltip"));
	settingsButton->setProperty("themeID", "configIconSmall");
	settingsButton->setProperty("class", "icon-gear");
	settingsButton->setMinimumSize(S(Constants::BUTTON_MIN_WIDTH), S(Constants::BUTTON_MIN_HEIGHT));
	settingsButton->setMaximumSize(S(Constants::BUTTON_MAX_WIDTH), S(Constants::BUTTON_MAX_HEIGHT));
	settingsButton->setIconSize(QSize(S(Constants::BUTTON_ICON_SIZE), S(Constants::BUTTON_ICON_SIZE)));
	settingsButton->style()->unpolish(settingsButton);
	settingsButton->style()->polish(settingsButton);

	// Configure the annotation button
	annotationButton->setObjectName("annotationButton");
	annotationButton->setIcon(QIcon(":images/annotation-icon.svg"));
	annotationButton->setMinimumSize(S(Constants::BUTTON_MIN_WIDTH), S(Constants::BUTTON_MIN_HEIGHT));
	annotationButton->setMaximumSize(S(Constants::BUTTON_MAX_WIDTH), S(Constants::BUTTON_MAX_HEIGHT));
	annotationButton->setIconSize(QSize(S(Constants::BUTTON_ICON_SIZE), S(Constants::BUTTON_ICON_SIZE)));
	annotationButton->setToolTip(obs_module_text("AnnotationButtonTooltip"));

	// Add the Save Chapter Marker button and a stretch to push the settings button to the right
	saveButtonLayout->addWidget(saveChapterMarkerButton);
	saveButtonLayout->addStretch();
	saveButtonLayout->addWidget(annotationButton);
	saveButtonLayout->addSpacing(S(4));
	saveButtonLayout->addWidget(settingsButton);

	saveButtonLayout->setAlignment(saveChapterMarkerButton, Qt::AlignLeft);
	saveButtonLayout->setAlignment(annotationButton, Qt::AlignRight);
	saveButtonLayout->setAlignment(settingsButton, Qt::AlignRight);

	mainLayout->addLayout(saveButtonLayout);
}

void ChapterMarkerDock::setupMainDockFeedbackLabel(QVBoxLayout *mainLayout)
{
	feedbackLabel->setObjectName("feedbackLabel");
	feedbackLabel->setProperty("themeID", "good");
	style()->polish(feedbackLabel);
	feedbackLabel->setWordWrap(true);
	mainLayout->addWidget(feedbackLabel);
}

void ChapterMarkerDock::setupMainDockPreviousChaptersGroup(QVBoxLayout *mainLayout)
{
	previousChaptersGroup = new QGroupBox(obs_module_text("PreviousChapters"), this);
	previousChaptersGroup->setObjectName("previousChaptersGroup");
	previousChaptersGroup->setToolTip(obs_module_text("PreviousChaptersTooltip"));
	QVBoxLayout *previousChaptersLayout = new QVBoxLayout(previousChaptersGroup);
	previousChaptersLayout->setObjectName("previousChaptersLayout");
	previousChaptersLayout->setAlignment(Qt::AlignTop | Qt::AlignLeft);
	previousChaptersList->setObjectName("previousChaptersList");
	// Dock content stays palette()-driven on purpose: the main dock follows
	// the user's OBS theme (native buttons, themeID icons); Catppuccin is for
	// the StreamUP-branded dialogs only.
	previousChaptersList->setStyleSheet(scale_qss(
		"QListWidget { background: palette(base); border: 1px solid palette(mid); border-radius: 4px; }"
		"QListWidget::item { padding: 4px 8px; }"
		"QListWidget::item:hover { background: palette(midlight); }"));
	previousChaptersLayout->addWidget(previousChaptersList);
	previousChaptersGroup->setLayout(previousChaptersLayout);

	mainLayout->addWidget(previousChaptersGroup);
}

void ChapterMarkerDock::refreshMainDockUI()
{
	previousChaptersGroup->setVisible(showPreviousChaptersEnabled);
}

//--------------------MAIN DOCK UI EVENT HANDLERS--------------------
void ChapterMarkerDock::onAddChapterMarkerButton()
{
	if (!obs_frontend_recording_active()) {
		blog(LOG_WARNING, "[StreamUP Record Chapter Manager] Recording is not active. Chapter marker not added.");
		showFeedbackMessage(obs_module_text("ChapterMarkerNotActive"), true);
		return;
	}

	if (!exportChaptersToFileEnabled && !insertChapterMarkersInVideoEnabled) {
		showFeedbackMessage(obs_module_text("NoExportMethod"), true);
		return;
	}

	QString chapterName = getChapterName();
	if (chapterName.isEmpty()) {
		// Use the default chapter name if the user did not provide one
		if (useIncrementalChapterNames) {
			chapterName = defaultChapterName + " " + QString::number(chapterCount);
			chapterCount++;
		} else {
			chapterName = defaultChapterName;
		}
	}

	addChapterMarker(chapterName, obs_module_text("SourceManual"));
	chapterNameInput->clear();
}

void ChapterMarkerDock::onSettingsClicked()
{
	if (!settingsDialog) {
		settingsDialog = createSettingsUI();
	}
	settingsDialog->show();
	settingsDialog->raise();
	settingsDialog->activateWindow();
}

void ChapterMarkerDock::onRecordingStopped()
{
	if (!exportChaptersToFileEnabled && !insertChapterMarkersInVideoEnabled) {
		showFeedbackMessage(obs_module_text("NoExportMethod"), true);
		return;
	}

	currentChapterNameLabel->setText(obs_module_text("RecordingNotActive"));
	currentChapterNameLabel->setProperty("themeID", "error");
	currentChapterNameLabel->style()->unpolish(currentChapterNameLabel);
	currentChapterNameLabel->style()->polish(currentChapterNameLabel);

	showFeedbackMessage(obs_module_text("RecordingFinished"), false);

	QString timestamp = getCurrentRecordingTime();
	QString endText = obs_module_text("End");
	QString recordingText = obs_module_text("Recording");

	// Write "End" marker to all enabled export formats
	writeChapterToTextFile(endText, timestamp, recordingText);
	writeChapterToFCPXMLFile(endText, timestamp, recordingText);
	writeChapterToPremiereXMLFile(endText, timestamp, recordingText);
	writeChapterToEDLFile(endText, timestamp, recordingText);

	// Close XML files with proper closing tags
	closeFCPXMLFile();
	closePremiereXMLFile();

	clearPreviousChaptersGroup();
	blog(LOG_INFO, "[StreamUP Record Chapter Manager] chapterCount: %d", chapterCount);

	incompatibleFileTypeMessageShown = false;
	chapterCount = Constants::DEFAULT_CHAPTER_COUNT; // Reset chapter count
	edlEventNumber = 1; // Reset EDL event number for next recording
	fcpMarkerID = 1; // Reset FCP marker ID for next recording
}

void ChapterMarkerDock::onPreviousChapterSelected()
{
	QListWidgetItem *currentItem = previousChaptersList->currentItem();
	if (currentItem) {
		chapterNameInput->setText(currentItem->text());
	}
}

void ChapterMarkerDock::onPreviousChapterDoubleClicked(QListWidgetItem *item)
{
	if (item) {
		chapterNameInput->setText(item->text());
		saveChapterMarkerButton->click();
	}
}

void ChapterMarkerDock::clearPreviousChaptersGroup()
{
	previousChaptersList->clear();
	chapters.clear();
	timestamps.clear();
}

void ChapterMarkerDock::loadAnnotationDock()
{
	const auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	obs_frontend_push_ui_translation(obs_module_get_string);

	if (!annotationDock) {
		annotationDock = new AnnotationDock(this, main_window);

		const QString title = QString::fromUtf8(obs_module_text("StreamUPChapterAnnotations"));
		const auto name = "AnnotationDock";

		obs_frontend_add_dock_by_id(name, QT_TO_UTF8(title), annotationDock);

		obs_frontend_pop_ui_translation();
	}
}

void ChapterMarkerDock::onAnnotationClicked(bool refresh)
{
	if (!exportChaptersToFileEnabled && !refresh) {
		showFeedbackMessage(obs_module_text("AnnotationErrorExportNotActive"), true);
		return;
	}

	if (!refresh) {
		annotationDock->parentWidget()->show();
		annotationDock->parentWidget()->raise();
	}
}

void ChapterMarkerDock::setAnnotationDock(AnnotationDock *dock)
{
	annotationDock = dock;
}

//--------------------MAIN DOCK LABELS--------------------
void ChapterMarkerDock::updateCurrentChapterLabel(const QString &chapterName)
{
	if (!exportChaptersToFileEnabled && !insertChapterMarkersInVideoEnabled) {
		showFeedbackMessage(obs_module_text("NoExportMethod"), true);
		return;
	}

	QFontMetrics metrics(currentChapterNameLabel->font());
	int labelWidth = currentChapterNameLabel->width();

	QString elidedText = metrics.elidedText(chapterName, Qt::ElideRight, labelWidth);

	currentChapterNameLabel->setText(elidedText);

	currentChapterNameLabel->setProperty("themeID", "good");
	currentChapterNameLabel->style()->unpolish(currentChapterNameLabel);
	currentChapterNameLabel->style()->polish(currentChapterNameLabel);
}

void ChapterMarkerDock::showFeedbackMessage(const QString &message, bool isError)
{
	QFontMetrics metrics(feedbackLabel->font());
	int labelWidth = feedbackLabel->width();

	QString elidedText = metrics.elidedText(message, Qt::ElideRight, labelWidth);

	feedbackLabel->setWordWrap(true);
	feedbackLabel->setText(elidedText);

	if (isError) {
		setChapterMarkerFeedbackLabel(elidedText, "error");
	} else {
		setChapterMarkerFeedbackLabel(elidedText, "good");
	}

	feedbackTimer.start();
}

//--------------------SETTINGS UI--------------------
QDialog *ChapterMarkerDock::createSettingsUI()
{
	settingsDialog = new ShadowDialog(this);
	// brandFooter=true → SoT brand line in the footer AND a taskbar button /
	// hover-thumbnail (WS_EX_APPWINDOW) for this primary window.
	WindowShell chrome = applyChrome(static_cast<ShadowDialog *>(settingsDialog),
					obs_module_text("ChapterMarkerManagerSettings"),
					QString("v%1").arg(PROJECT_VERSION), /*brandFooter=*/true,
					"StreamUP Chapter Marker Manager");
	QVBoxLayout *mainLayout = chrome.content;
	mainLayout->setContentsMargins(S(20), S(16), S(20), S(16)); // applyChrome content has no padding

	// Two-column layout
	QHBoxLayout *columnsLayout = new QHBoxLayout();
	columnsLayout->setSpacing(S(20));

	QVBoxLayout *leftCol = new QVBoxLayout();
	leftCol->setSpacing(S(14));
	QVBoxLayout *rightCol = new QVBoxLayout();
	rightCol->setSpacing(S(14));

	setupSettingsGeneralGroup(leftCol);
	setupSettingsExportGroup(rightCol);
	setupSettingsAutoChapterGroup(rightCol);

	rightCol->addStretch();
	leftCol->addStretch();

	columnsLayout->addLayout(leftCol, 1);
	columnsLayout->addLayout(rightCol, 1);
	mainLayout->addLayout(columnsLayout);

	// Action buttons in the footer's right-anchored slot, inline with the brand
	// line. Cancel = outline.
	QPushButton *okBtn = new PillButton("Save", "primary");
	QPushButton *cancelBtn = new PillButton("Cancel", "outline");
	chrome.footerButtons->addWidget(cancelBtn);
	chrome.footerButtons->addWidget(okBtn);

	connect(okBtn, &QPushButton::clicked, this, &ChapterMarkerDock::saveSettingsAndCloseDialog);
	connect(cancelBtn, &QPushButton::clicked, settingsDialog, &QDialog::reject);

	settingsDialog->setMinimumWidth(S(600));
	settingsDialog->adjustSize();

	return settingsDialog;
}

void ChapterMarkerDock::setupSettingsGeneralGroup(QVBoxLayout *mainLayout)
{
	QWidget *generalSettingsGroup = new QWidget(settingsDialog);
	QVBoxLayout *generalSettingsLayout = new QVBoxLayout(generalSettingsGroup);
	generalSettingsLayout->addWidget(sectionHeader(obs_module_text("GeneralSettings")));

	QLabel *defaultChapterNameLabel = new QLabel(obs_module_text("GeneralSettingsDefaultChapterName"), generalSettingsGroup);
	defaultChapterNameLabel->setToolTip(obs_module_text("DefaultChapterNameTooltip"));
	defaultChapterNameLabel->setStyleSheet(labelStyle());

	defaultChapterNameEdit = new QLineEdit(generalSettingsGroup);
	defaultChapterNameEdit->setPlaceholderText(obs_module_text("GeneralSettingsDefaultChapterPlaceholder"));
	defaultChapterNameEdit->setToolTip(obs_module_text("DefaultChapterNameTooltip"));
	defaultChapterNameEdit->setText(defaultChapterName);
	defaultChapterNameEdit->setMinimumWidth(S(200));
	defaultChapterNameEdit->setStyleSheet(lineEditStyle());

	// Create a horizontal layout to place the label and text box side by side
	QHBoxLayout *chapterNameLayout = new QHBoxLayout;
	chapterNameLayout->addWidget(defaultChapterNameLabel);
	chapterNameLayout->addWidget(defaultChapterNameEdit);

	// Add the horizontal layout to the general settings layout
	generalSettingsLayout->addLayout(chapterNameLayout);

	SwitchButton *useIncrementalChapterNamesSwitch =
		makeSwitch(obs_module_text("UseIncrementalChapterNames"), generalSettingsGroup);
	useIncrementalChapterNamesSwitch->setToolTip(obs_module_text("UseIncrementalChapterNamesTooltip"));
	generalSettingsLayout->addWidget(useIncrementalChapterNamesSwitch);
	useIncrementalChapterNamesSwitch->setChecked(useIncrementalChapterNames);
	connect(useIncrementalChapterNamesSwitch, &SwitchButton::toggled, this,
		[this](bool checked) { useIncrementalChapterNames = checked; });

	showPreviousChaptersCheckbox = makeSwitch(obs_module_text("GeneralSettingsShowChapterHistory"), generalSettingsGroup);
	showPreviousChaptersCheckbox->setToolTip(obs_module_text("PreviousChaptersTooltip"));
	generalSettingsLayout->addWidget(showPreviousChaptersCheckbox);
	showPreviousChaptersCheckbox->setChecked(showPreviousChaptersEnabled);

	fullChapterHistoryCheckbox = makeSwitch(obs_module_text("GeneralSettingsFullChapterHistory"), generalSettingsGroup);
	fullChapterHistoryCheckbox->setToolTip(obs_module_text("FullChapterHistoryTooltip"));
	generalSettingsLayout->addWidget(fullChapterHistoryCheckbox);
	fullChapterHistoryCheckbox->setChecked(fullChapterHistoryEnabled);

	addChapterSourceCheckbox = makeSwitch(obs_module_text("GeneralSettingsAddChapterSource"), generalSettingsGroup);
	addChapterSourceCheckbox->setToolTip(obs_module_text("GeneralSettingsAddChapterSourceTooltip"));
	generalSettingsLayout->addWidget(addChapterSourceCheckbox);
	addChapterSourceCheckbox->setChecked(addChapterSourceEnabled);

	setPresetChaptersButton = new PillButton(obs_module_text("GeneralSettingsSetPresetHotkeys"), "outline");
	setPresetChaptersButton->setToolTip(obs_module_text("GeneralSettingsSetPresetHotkeysTooltip"));
	connect(setPresetChaptersButton, &QPushButton::clicked, this, &ChapterMarkerDock::onSetPresetChaptersButtonClicked);
	generalSettingsLayout->addWidget(setPresetChaptersButton);

	generalSettingsGroup->setLayout(generalSettingsLayout);
	generalSettingsGroup->adjustSize();

	// Set the size policy to Preferred for width and Fixed for height
	generalSettingsGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

	mainLayout->addWidget(generalSettingsGroup);
}

void ChapterMarkerDock::setupSettingsExportGroup(QVBoxLayout *mainLayout)
{
	exportSettingsGroup = new QWidget();
	exportSettingsLayout = new QVBoxLayout(exportSettingsGroup);
	exportSettingsLayout->addWidget(sectionHeader(obs_module_text("Export")));

	// Insert chapter markers into video file checkbox
	insertChapterMarkersCheckbox = makeSwitch(obs_module_text("ExportSettingsInsertIntoFile"), exportSettingsGroup);
	insertChapterMarkersCheckbox->setToolTip(obs_module_text("ExportSettingsInsertIntoFileTooltip"));
	exportSettingsLayout->addWidget(insertChapterMarkersCheckbox);
	insertChapterMarkersCheckbox->setChecked(insertChapterMarkersInVideoEnabled);

	// Create check boxes
	exportChaptersToFileCheckbox = makeSwitch(obs_module_text("ExportSettingsExportToFile"), exportSettingsGroup);
	exportChaptersToFileCheckbox->setToolTip(obs_module_text("ExportSettingsExportToFileTooltip"));
	exportChaptersToTextCheckbox = makeSwitch(obs_module_text("ExportSettingsExportToText"), exportSettingsGroup);
	exportChaptersToTextCheckbox->setToolTip(obs_module_text("ExportSettingsExportToTextTooltip"));
	exportChaptersToFCPXMLCheckbox = makeSwitch(obs_module_text("ExportSettingsExportToFCPXml"), exportSettingsGroup);
	exportChaptersToFCPXMLCheckbox->setToolTip(obs_module_text("ExportSettingsExportToFCPXmlTooltip"));
	exportChaptersToPremiereXMLCheckbox = makeSwitch(obs_module_text("ExportSettingsExportToPremiereXml"), exportSettingsGroup);
	exportChaptersToPremiereXMLCheckbox->setToolTip(obs_module_text("ExportSettingsExportToPremiereXmlTooltip"));
	exportChaptersToEDLCheckbox = makeSwitch(obs_module_text("ExportSettingsExportToEDL"), exportSettingsGroup);
	exportChaptersToEDLCheckbox->setToolTip(obs_module_text("ExportSettingsExportToEDLTooltip"));

	// Set check boxes visually
	exportChaptersToFileCheckbox->setChecked(exportChaptersToFileEnabled);
	exportChaptersToTextCheckbox->setChecked(exportChaptersToTextEnabled);
	exportChaptersToFCPXMLCheckbox->setChecked(exportChaptersToFCPXMLEnabled);
	exportChaptersToPremiereXMLCheckbox->setChecked(exportChaptersToPremiereXMLEnabled);
	exportChaptersToEDLCheckbox->setChecked(exportChaptersToEDLEnabled);
	connect(exportChaptersToFileCheckbox, &SwitchButton::toggled, this, &ChapterMarkerDock::onExportChaptersToFileToggled);

	exportSettingsLayout->addWidget(exportChaptersToFileCheckbox);

	// Indented sub-options under "Export to file" — one container with a single
	// left indent so every sub-toggle's switch lines up in one column. Shown/
	// hidden as a unit with the parent toggle.
	exportSubWidget = new QWidget();
	QVBoxLayout *exportSubLayout = new QVBoxLayout(exportSubWidget);
	exportSubLayout->setContentsMargins(S(Constants::INDENT_SPACING), 0, 0, 0);
	exportSubLayout->setSpacing(S(8));
	exportSubLayout->addWidget(exportChaptersToTextCheckbox);
	exportSubLayout->addWidget(exportChaptersToFCPXMLCheckbox);
	exportSubLayout->addWidget(exportChaptersToPremiereXMLCheckbox);
	exportSubLayout->addWidget(exportChaptersToEDLCheckbox);
	exportSubWidget->setVisible(exportChaptersToFileEnabled);
	exportSettingsLayout->addWidget(exportSubWidget);

	exportSettingsGroup->setLayout(exportSettingsLayout);

	// Set the size policy to Preferred for width and Fixed for height
	exportSettingsGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

	mainLayout->addWidget(exportSettingsGroup);
}

void ChapterMarkerDock::setupSettingsAutoChapterGroup(QVBoxLayout *mainLayout)
{
	sceneChangeSettingsGroup = new QWidget(settingsDialog);
	QVBoxLayout *sceneChangeSettingsLayout = new QVBoxLayout(sceneChangeSettingsGroup);
	sceneChangeSettingsLayout->addWidget(sectionHeader(obs_module_text("AutoChapterSettings")));

	// Add chapter on scene change checkbox
	chapterOnSceneChangeCheckbox = makeSwitch(obs_module_text("AutoChapterOnSceneChange"), sceneChangeSettingsGroup);
	chapterOnSceneChangeCheckbox->setToolTip(obs_module_text("AutoChapterOnSceneChangeTooltip"));
	sceneChangeSettingsLayout->addWidget(chapterOnSceneChangeCheckbox);
	chapterOnSceneChangeCheckbox->setChecked(chapterOnSceneChangeEnabled);
	connect(chapterOnSceneChangeCheckbox, &SwitchButton::toggled, this, &ChapterMarkerDock::onChapterOnSceneChangeToggled);

	// Set ignore scene button
	setIgnoredScenesButton = new PillButton(obs_module_text("AutoChapterSetIgnoredScenes"), "outline");
	setIgnoredScenesButton->setToolTip(obs_module_text("AutoChapterSetIgnoredScenesTooltip"));
	setIgnoredScenesButton->setVisible(chapterOnSceneChangeEnabled);
	connect(setIgnoredScenesButton, &QPushButton::clicked, this, &ChapterMarkerDock::onSetIgnoredScenesClicked);
	sceneChangeSettingsLayout->addWidget(setIgnoredScenesButton);

	sceneChangeSettingsGroup->setLayout(sceneChangeSettingsLayout);
	sceneChangeSettingsGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

	mainLayout->addWidget(sceneChangeSettingsGroup);
}

void ChapterMarkerDock::saveSettingsAndCloseDialog()
{
	exportChaptersToFileEnabled = exportChaptersToFileCheckbox->isChecked();
	if (annotationDock) {
		annotationDock->updateInputState(exportChaptersToFileEnabled); // Update input state
	}
	SaveSettings();

	obs_data_t *settings = SaveLoadSettingsCallback(nullptr, false);

	if (settings) {
		LoadSettings(settings);
		obs_data_release(settings);
	}

	refreshMainDockUI();
	settingsDialog->accept();
}

//--------------------SETTINGS UI EVENT HANDLERS--------------------
void ChapterMarkerDock::onExportChaptersToFileToggled(bool checked)
{
	exportChaptersToFileEnabled = checked;
	if (exportSubWidget)
		exportSubWidget->setVisible(checked);

	QSize size = exportSettingsGroup->sizeHint();
	int newHeight = size.height();
	exportSettingsGroup->setFixedHeight(newHeight);

	settingsDialog->adjustSize();
}

void ChapterMarkerDock::onChapterOnSceneChangeToggled(bool checked)
{
	setIgnoredScenesButton->setVisible(checked);

	QSize size = sceneChangeSettingsGroup->sizeHint();
	int newHeight = size.height();
	sceneChangeSettingsGroup->setFixedHeight(newHeight);

	settingsDialog->adjustSize();
}

void ChapterMarkerDock::onSetIgnoredScenesClicked()
{
	if (!ignoredScenesDialog) {
		ignoredScenesDialog = createIgnoredScenesUI();
	}
	ignoredScenesDialog->show();
	ignoredScenesDialog->raise();
	ignoredScenesDialog->activateWindow();
}

void ChapterMarkerDock::onSetPresetChaptersButtonClicked()
{
	if (!presetChaptersDialog) {
		setupPresetChaptersDialog();
	}

	// Populate the chaptersListWidget with presetChapters
	chaptersListWidget->clear();
	for (const auto &chapter : presetChapters) {
		chaptersListWidget->addItem(chapter);
	}

	presetChaptersDialog->show();
	presetChaptersDialog->raise();
	presetChaptersDialog->activateWindow();
}

//--------------------PRESET CHAPTERS--------------------
void ChapterMarkerDock::setupPresetChaptersDialog()
{
	presetChaptersDialog = new ShadowDialog(this);
	WindowShell chrome = applyChrome(static_cast<ShadowDialog *>(presetChaptersDialog),
					 obs_module_text("GeneralSettingsSetPresetHotkeys"),
					 QString("v%1").arg(PROJECT_VERSION), /*brandFooter=*/true,
					 "StreamUP Chapter Marker Manager");
	QVBoxLayout *dialogLayout = chrome.content;
	dialogLayout->setContentsMargins(S(20), S(16), S(20), S(16)); // applyChrome content has no padding
	dialogLayout->setSpacing(S(12));

	// Explanation label
	QLabel *explanationLabel = new QLabel(obs_module_text("GeneralSettingsSetPresetChaptersExplanation"), presetChaptersDialog);
	explanationLabel->setWordWrap(true);
	explanationLabel->setStyleSheet(labelStyle());
	dialogLayout->addWidget(explanationLabel);

	// Input field for new chapter names
	presetChapterNameInput = new QLineEdit(presetChaptersDialog);
	presetChapterNameInput->setPlaceholderText(obs_module_text("SetPresetHotkeysChapterNameInput"));
	presetChapterNameInput->setToolTip(obs_module_text("SetPresetHotkeysChapterNameInputTooltip"));
	presetChapterNameInput->setStyleSheet(lineEditStyle());
	dialogLayout->addWidget(presetChapterNameInput);

	// Add and Remove buttons
	QHBoxLayout *buttonsLayout = new QHBoxLayout();
	buttonsLayout->setSpacing(S(10));
	addChapterButton = new PillButton(obs_module_text("SetPresetHotkeysAddChapter"), "primary");
	addChapterButton->setToolTip(obs_module_text("SetPresetHotkeysAddChapterTooltip"));
	removeChapterButton = new PillButton(obs_module_text("SetPresetHotkeyRemoveChapter"), "danger");
	removeChapterButton->setToolTip(obs_module_text("SetPresetHotkeyRemoveChapterTooltip"));
	buttonsLayout->addWidget(addChapterButton);
	buttonsLayout->addWidget(removeChapterButton);
	dialogLayout->addLayout(buttonsLayout);

	// List widget to display chapters
	chaptersListWidget = new QListWidget(presetChaptersDialog);
	chaptersListWidget->setStyleSheet(listStyle());
	chaptersListWidget->setMinimumHeight(S(160));
	useScrollBars(chaptersListWidget);
	dialogLayout->addWidget(chaptersListWidget);

	// Connections for buttons
	connect(addChapterButton, &QPushButton::clicked, this, [this]() {
		QString chapterName = presetChapterNameInput->text().trimmed();
		if (!chapterName.isEmpty() && !presetChapters.contains(chapterName)) {
			presetChapters.append(chapterName);
			chaptersListWidget->addItem(chapterName);
			presetChapterNameInput->clear();
			registerChapterHotkey(chapterName);
		}
	});

	connect(removeChapterButton, &QPushButton::clicked, this, [this]() {
		QList<QListWidgetItem *> selectedItems = chaptersListWidget->selectedItems();
		for (QListWidgetItem *item : selectedItems) {
			QString chapterName = item->text();
			presetChapters.removeOne(chapterName);
			delete item;
			unregisterChapterHotkey(chapterName);
		}
	});

	// Footer buttons in the right-anchored slot (Cancel = outline).
	QPushButton *okBtn = new PillButton("Done", "primary");
	QPushButton *cancelBtn = new PillButton("Cancel", "outline");
	chrome.footerButtons->addWidget(cancelBtn);
	chrome.footerButtons->addWidget(okBtn);

	connect(okBtn, &QPushButton::clicked, presetChaptersDialog, &QDialog::accept);
	connect(cancelBtn, &QPushButton::clicked, presetChaptersDialog, &QDialog::reject);

	presetChaptersDialog->setMinimumWidth(S(440));
}

void ChapterMarkerDock::registerChapterHotkey(const QString &chapterName)
{
	if (!chapterHotkeys.contains(chapterName)) {
		obs_hotkey_id hotkeyId = obs_hotkey_register_frontend(QT_TO_UTF8(chapterName), QT_TO_UTF8(chapterName),
								      AddChapterMarkerHotkey, this);

		if (hotkeyId != OBS_INVALID_HOTKEY_ID) {
			chapterHotkeys.insert(chapterName, hotkeyId);
		}
	}
}

void ChapterMarkerDock::unregisterChapterHotkey(const QString &chapterName)
{
	if (chapterHotkeys.contains(chapterName)) {
		obs_hotkey_id hotkeyId = chapterHotkeys.value(chapterName);
		obs_hotkey_unregister(hotkeyId);
		chapterHotkeys.remove(chapterName);
	}
}

void ChapterMarkerDock::SaveChapterHotkeys(obs_data_t *settings)
{
	obs_data_array_t *hotkeysArray = obs_data_array_create();
	for (const auto &chapterName : chapterHotkeys.keys()) {
		obs_data_t *hotkeyData = obs_data_create();
		obs_data_set_string(hotkeyData, "chapterName", QT_TO_UTF8(chapterName));

		obs_data_array_t *hotkeySaveArray = obs_hotkey_save(chapterHotkeys[chapterName]);
		obs_data_set_array(hotkeyData, "hotkeyData", hotkeySaveArray);
		obs_data_array_release(hotkeySaveArray);

		obs_data_array_push_back(hotkeysArray, hotkeyData);
		obs_data_release(hotkeyData);
	}
	obs_data_set_array(settings, "chapterHotkeys", hotkeysArray);
	obs_data_array_release(hotkeysArray);
}

void ChapterMarkerDock::LoadChapterHotkeys(obs_data_t *settings)
{
	obs_data_array_t *hotkeysArray = obs_data_get_array(settings, "chapterHotkeys");
	if (!hotkeysArray)
		return;

	for (size_t i = 0; i < obs_data_array_count(hotkeysArray); ++i) {
		obs_data_t *hotkeyData = obs_data_array_item(hotkeysArray, i);
		const char *chapterName = obs_data_get_string(hotkeyData, "chapterName");
		obs_data_array_t *hotkeyLoadArray = obs_data_get_array(hotkeyData, "hotkeyData");

		if (chapterName && hotkeyLoadArray) {
			obs_hotkey_id hotkeyId =
				obs_hotkey_register_frontend(chapterName, chapterName, AddChapterMarkerHotkey, this);

			if (hotkeyId != OBS_INVALID_HOTKEY_ID) {
				obs_hotkey_load(hotkeyId, hotkeyLoadArray);
				chapterHotkeys.insert(QString::fromUtf8(chapterName), hotkeyId);
			}
			obs_data_array_release(hotkeyLoadArray);
		}
		obs_data_release(hotkeyData);
	}
	obs_data_array_release(hotkeysArray);
}

void ChapterMarkerDock::LoadPresetChapters(obs_data_t *settings)
{
	obs_data_array_t *chaptersArray = obs_data_get_array(settings, "presetChapters");
	if (chaptersArray) {
		for (size_t i = 0; i < obs_data_array_count(chaptersArray); ++i) {
			obs_data_t *chapterData = obs_data_array_item(chaptersArray, i);
			const char *chapterName = obs_data_get_string(chapterData, "chapterName");

			if (chapterName) {
				presetChapters.append(QString::fromUtf8(chapterName));
			}
			obs_data_release(chapterData);
		}
		obs_data_array_release(chaptersArray);
	}
}

//--------------------IGNORED SCENES UI--------------------
QDialog *ChapterMarkerDock::createIgnoredScenesUI()
{
	QDialog *dialog = new ShadowDialog(this);
	WindowShell chrome = applyChrome(static_cast<ShadowDialog *>(dialog),
					 obs_module_text("IgnoredScenes"),
					 QString("v%1").arg(PROJECT_VERSION), /*brandFooter=*/true,
					 "StreamUP Chapter Marker Manager");
	QVBoxLayout *mainLayout = chrome.content;
	mainLayout->setContentsMargins(S(20), S(16), S(20), S(16)); // applyChrome content has no padding

	ignoredScenesListWidget = new QListWidget(dialog);
	ignoredScenesListWidget->setSelectionMode(QAbstractItemView::MultiSelection);
	ignoredScenesListWidget->setToolTip(obs_module_text("IgnoredScenesTooltip"));
	ignoredScenesListWidget->setStyleSheet(listStyle());
	useScrollBars(ignoredScenesListWidget);

	// Disable horizontal scrolling
	ignoredScenesListWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

	// Populate the list widget with OBS scene names
	populateIgnoredScenesListWidget();

	// Adjust the width of the list widget to fit the contents
	int longestNameWidth = ignoredScenesListWidget->sizeHintForColumn(0);
	ignoredScenesListWidget->setFixedWidth(longestNameWidth + S(30)); // Add some padding

	// Calculate the height for 10 entries
	int rowHeight = ignoredScenesListWidget->sizeHintForRow(0);
	int listHeight = rowHeight * 10;
	ignoredScenesListWidget->setFixedHeight(listHeight);

	mainLayout->addWidget(ignoredScenesListWidget);

	// Footer buttons in the right-anchored slot (Cancel = outline).
	QPushButton *okBtn = new PillButton("OK", "primary");
	QPushButton *cancelBtn = new PillButton("Cancel", "outline");
	chrome.footerButtons->addWidget(cancelBtn);
	chrome.footerButtons->addWidget(okBtn);

	connect(okBtn, &QPushButton::clicked, this, &ChapterMarkerDock::saveIgnoredScenes);
	connect(cancelBtn, &QPushButton::clicked, dialog, &QDialog::reject);

	// Set the size policy to Preferred for width and Fixed for height
	dialog->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

	// Adjust the dialog size initially to fit the contents
	dialog->adjustSize();

	return dialog;
}

void ChapterMarkerDock::populateIgnoredScenesListWidget()
{
	ignoredScenesListWidget->clear();

	char **scene_names = obs_frontend_get_scene_names();
	if (scene_names) {
		for (char **name = scene_names; *name; name++) {
			QListWidgetItem *item = new QListWidgetItem(QString::fromUtf8(*name), ignoredScenesListWidget);
			// Check if this scene is in the ignoredScenes list and select it if it is
			if (ignoredScenes.contains(QString::fromUtf8(*name))) {
				item->setSelected(true);
			}
		}
		bfree(scene_names);
	}
}

void ChapterMarkerDock::saveIgnoredScenes()
{
	ignoredScenes.clear();
	QList<QListWidgetItem *> selectedItems = ignoredScenesListWidget->selectedItems();
	for (QListWidgetItem *item : selectedItems) {
		ignoredScenes << item->text();
	}

	// Save the settings
	SaveSettings();

	if (ignoredScenesDialog) {
		ignoredScenesDialog->accept();
	}
}

//--------------------FILE MANAGEMENT--------------------
void ChapterMarkerDock::createExportFiles()
{
	if (!exportChaptersToFileEnabled) {
		return;
	}

	obs_output_t *output = obs_frontend_get_recording_output();
	if (!output) {
		blog(LOG_ERROR, "[StreamUP Record Chapter Manager] Could not get the recording output.");
		return;
	}

	obs_data_t *settings = obs_output_get_settings(output);
	if (!settings) {
		blog(LOG_ERROR, "[StreamUP Record Chapter Manager] Could not get the recording output settings.");
		obs_output_release(output);
		return;
	}

	const char *recording_path = obs_data_get_string(settings, "path");
	if (!recording_path || strlen(recording_path) == 0) {
		blog(LOG_ERROR, "[StreamUP Record Chapter Manager] Could not get the recording output path.");
		obs_data_release(settings);
		obs_output_release(output);
		return;
	}

	QString outputPath = QString::fromUtf8(recording_path);
	obs_data_release(settings);
	obs_output_release(output);

	blog(LOG_INFO, "[StreamUP Record Chapter Manager] Recording path: %s", QT_TO_UTF8(outputPath));

	QFileInfo fileInfo(outputPath);
	QString baseName = fileInfo.completeBaseName();
	QString directoryPath = fileInfo.absolutePath();

	recordingMediaPath = outputPath;
	recordingMediaBaseName = baseName;

	if (exportChaptersToTextEnabled) {
		const QString chapterFilePath = directoryPath + "/" + baseName + Constants::TEXT_FILE_SUFFIX;
		QFile file(chapterFilePath);
		if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
			QTextStream out(&file);
			out << "Chapter Markers for " << baseName << "\n";
			file.close();
			blog(LOG_INFO, "[StreamUP Record Chapter Manager] Created chapter file: %s", QT_TO_UTF8(chapterFilePath));
			setExportTextFilePath(chapterFilePath);
		} else {
			blog(LOG_ERROR, "[StreamUP Record Chapter Manager] Failed to create chapter file: %s",
			     QT_TO_UTF8(chapterFilePath));
		}
	}

	if (exportChaptersToFCPXMLEnabled) {
		const QString fcpXmlFilePath = directoryPath + "/" + baseName + Constants::FCPXML_FILE_SUFFIX;
		// FCPXML requires the asset duration up-front, so we buffer markers in
		// memory during recording and write the full document in closeFCPXMLFile().
		fcpChapters.clear();
		setExportFCPXMLFilePath(fcpXmlFilePath);
		blog(LOG_INFO, "[StreamUP Record Chapter Manager] FCPXML chapter file path set: %s", QT_TO_UTF8(fcpXmlFilePath));
	}

	if (exportChaptersToPremiereXMLEnabled) {
		const QString premiereXmlFilePath = directoryPath + "/" + baseName + Constants::PREMIEREXML_FILE_SUFFIX;
		QFile premiereXmlFile(premiereXmlFilePath);
		if (premiereXmlFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
			QTextStream out(&premiereXmlFile);
			int fps = getFrameRate();
			bool isNtsc = isNTSCDropFrame();

			out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
			out << "<!DOCTYPE xmeml>\n";
			out << "<xmeml version=\"4\">\n";
			out << "\t<sequence id=\"sequence-1\">\n";
			out << "\t\t<name>" << escapeXml(baseName) << "</name>\n";
			out << "\t\t<rate>\n";
			out << "\t\t\t<timebase>" << fps << "</timebase>\n";
			out << "\t\t\t<ntsc>" << (isNtsc ? "TRUE" : "FALSE") << "</ntsc>\n";
			out << "\t\t</rate>\n";
			out << "\t\t<media>\n";
			out << "\t\t\t<video>\n";
			out << "\t\t\t\t<track>\n";
			out << "\t\t\t\t\t<enabled>TRUE</enabled>\n";
			out << "\t\t\t\t\t<locked>FALSE</locked>\n";
			out << "\t\t\t\t</track>\n";
			out << "\t\t\t</video>\n";
			out << "\t\t</media>\n";
			out << "\t\t<timecode>\n";
			out << "\t\t\t<rate>\n";
			out << "\t\t\t\t<timebase>" << fps << "</timebase>\n";
			out << "\t\t\t\t<ntsc>" << (isNtsc ? "TRUE" : "FALSE") << "</ntsc>\n";
			out << "\t\t\t</rate>\n";
			out << "\t\t\t<string>00;00;00;00</string>\n";
			out << "\t\t\t<frame>0</frame>\n";
			out << "\t\t\t<displayformat>DF</displayformat>\n";
			out << "\t\t</timecode>\n";
			premiereXmlFile.close();
			blog(LOG_INFO, "[StreamUP Record Chapter Manager] Created Premiere XML chapter file: %s", QT_TO_UTF8(premiereXmlFilePath));
			setExportPremiereXMLFilePath(premiereXmlFilePath);
		} else {
			blog(LOG_ERROR, "[StreamUP Record Chapter Manager] Failed to create Premiere XML chapter file: %s",
			     QT_TO_UTF8(premiereXmlFilePath));
		}
	}

	if (exportChaptersToEDLEnabled) {
		const QString edlFilePath = directoryPath + "/" + baseName + Constants::EDL_FILE_SUFFIX;
		QFile edlFile(edlFilePath);
		if (edlFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
			QTextStream out(&edlFile);
			bool isDropFrame = isNTSCDropFrame();
			out << "TITLE: " << baseName << "\n";
			out << "FCM: " << (isDropFrame ? "DROP FRAME" : "NON-DROP FRAME") << "\n\n";
			edlFile.close();
			blog(LOG_INFO, "[StreamUP Record Chapter Manager] Created EDL chapter file: %s", QT_TO_UTF8(edlFilePath));
			setExportEDLFilePath(edlFilePath);
		} else {
			blog(LOG_ERROR, "[StreamUP Record Chapter Manager] Failed to create EDL chapter file: %s",
			     QT_TO_UTF8(edlFilePath));
		}
	}
}

void ChapterMarkerDock::setExportTextFilePath(const QString &filePath)
{
	exportTextFilePath = filePath;
}

void ChapterMarkerDock::writeChapterToTextFile(const QString &chapterName, const QString &timestamp, const QString &chapterSource)
{
	if (!exportChaptersToFileEnabled || !exportChaptersToTextEnabled) {
		return;
	}

	if (exportTextFilePath.isEmpty()) {
		blog(LOG_ERROR, "[StreamUP Record Chapter Manager] Chapter file path is not set, creating a new file.");
		createExportFiles();
		return;
	}

	QString fullChapterName = appendChapterSource(chapterName, chapterSource, addChapterSourceEnabled);

	QString content = QString("%1 - %2\n").arg(timestamp, fullChapterName);
	if (!writeToFile(exportTextFilePath, content)) {
		blog(LOG_ERROR, "[StreamUP Record Chapter Manager] Failed to open text file: %s", QT_TO_UTF8(exportTextFilePath));
	}
}

void ChapterMarkerDock::setExportFCPXMLFilePath(const QString &filePath)
{
	exportFCPXMLFilePath = filePath;
}

void ChapterMarkerDock::setExportPremiereXMLFilePath(const QString &filePath)
{
	exportPremiereXMLFilePath = filePath;
}

void ChapterMarkerDock::setExportEDLFilePath(const QString &filePath)
{
	exportEDLFilePath = filePath;
}

QString ChapterMarkerDock::convertTimestampToTimecode(const QString &timestamp, int frameNumber) const
{
	// Convert HH:MM:SS timestamp to HH:MM:SS:FF timecode format
	// Parse the timestamp
	QTime time = QTime::fromString(timestamp, "HH:mm:ss");
	if (!time.isValid()) {
		return "00:00:00:00"; // Default fallback
	}

	// Format: HH:MM:SS:FF where FF is frame number (00-29 for 30fps)
	// For markers, we use frame 00 for start and 01 for end (1 frame duration)
	return QString("%1:%2:%3:%4")
		.arg(time.hour(), 2, 10, QChar('0'))
		.arg(time.minute(), 2, 10, QChar('0'))
		.arg(time.second(), 2, 10, QChar('0'))
		.arg(frameNumber, 2, 10, QChar('0'));
}

void ChapterMarkerDock::writeChapterToFCPXMLFile(const QString &chapterName, const QString &timestamp, const QString &chapterSource)
{
	if (!exportChaptersToFileEnabled || !exportChaptersToFCPXMLEnabled) {
		return;
	}

	if (exportFCPXMLFilePath.isEmpty()) {
		blog(LOG_ERROR, "[StreamUP Record Chapter Manager] FCPXML file path is not set, creating a new file.");
		createExportFiles();
		return;
	}

	QTime time = QTime::fromString(timestamp, "HH:mm:ss");
	int totalSeconds = time.hour() * 3600 + time.minute() * 60 + time.second();

	// Derive the marker frame from the *actual* rational fps so it stays
	// consistent with how closeFCPXMLFile renders the time back out as
	// frameNumber * fpsDen / fpsNum. Using the rounded integer fps here
	// (30 for 29.97) drifts ~0.1% on true NTSC rates, growing with the
	// timestamp. Integer rates (30/60) stay exact.
	obs_video_info ovi = {};
	bool oviOk = obs_get_video_info(&ovi) && ovi.fps_num > 0 && ovi.fps_den > 0;
	uint32_t fpsNum = oviOk ? ovi.fps_num : 30000;
	uint32_t fpsDen = oviOk ? ovi.fps_den : 1000;
	int frameNumber = static_cast<int>(
		round(static_cast<double>(totalSeconds) * static_cast<double>(fpsNum) / static_cast<double>(fpsDen)));

	QString fullChapterName = appendChapterSource(chapterName, chapterSource, addChapterSourceEnabled);
	fcpChapters.append(qMakePair(frameNumber, fullChapterName));
	fcpMarkerID++;
}

void ChapterMarkerDock::writeChapterToPremiereXMLFile(const QString &chapterName, const QString &timestamp, const QString &chapterSource)
{
	if (!exportChaptersToFileEnabled || !exportChaptersToPremiereXMLEnabled) {
		return;
	}

	if (exportPremiereXMLFilePath.isEmpty()) {
		blog(LOG_ERROR, "[StreamUP Record Chapter Manager] Premiere XML file path is not set, creating a new file.");
		createExportFiles();
		return;
	}

	// Convert timestamp HH:MM:SS to frame number
	QTime time = QTime::fromString(timestamp, "HH:mm:ss");
	int totalSeconds = time.hour() * 3600 + time.minute() * 60 + time.second();
	int fps = getFrameRate();
	int frameNumber = totalSeconds * fps;

	QFile file(exportPremiereXMLFilePath);
	if (!file.open(QIODevice::Append | QIODevice::Text)) {
		blog(LOG_ERROR, "[StreamUP Record Chapter Manager] Failed to open Premiere XML file: %s", QT_TO_UTF8(exportPremiereXMLFilePath));
		return;
	}

	QString fullChapterName = appendChapterSource(chapterName, chapterSource, addChapterSourceEnabled);

	QTextStream out(&file);
	out << "\t\t<marker>\n";
	out << "\t\t\t<comment></comment>\n";
	out << "\t\t\t<name>" << escapeXml(fullChapterName) << "</name>\n";
	out << "\t\t\t<in>" << frameNumber << "</in>\n";
	out << "\t\t\t<out>-1</out>\n";
	out << "\t\t</marker>\n";
	file.close();
}

void ChapterMarkerDock::closeFCPXMLFile()
{
	if (!exportChaptersToFCPXMLEnabled || exportFCPXMLFilePath.isEmpty()) {
		return;
	}

	obs_video_info ovi = {};
	bool oviOk = obs_get_video_info(&ovi) && ovi.fps_num > 0 && ovi.fps_den > 0;
	uint32_t fpsNum = oviOk ? ovi.fps_num : 30000;
	uint32_t fpsDen = oviOk ? ovi.fps_den : 1000;
	uint32_t width = (oviOk && ovi.output_width > 0) ? ovi.output_width : 1920;
	uint32_t height = (oviOk && ovi.output_height > 0) ? ovi.output_height : 1080;

	// Asset/clip duration in frames: max marker frame, padded so the last
	// chapter has somewhere to live. Falls back to a non-zero duration if
	// no chapters were captured.
	int durationFrames = 0;
	for (const auto &m : fcpChapters) {
		if (m.first > durationFrames) {
			durationFrames = m.first;
		}
	}
	int padFrames = static_cast<int>(round(static_cast<double>(fpsNum) / static_cast<double>(fpsDen)));
	if (padFrames < 1) {
		padFrames = 1;
	}
	durationFrames += padFrames;
	if (durationFrames < padFrames) {
		durationFrames = padFrames;
	}

	const QString assetName = recordingMediaBaseName.isEmpty()
					  ? QFileInfo(exportFCPXMLFilePath).completeBaseName()
					  : recordingMediaBaseName;

	// FCPXML's <media-rep src> is required, so always produce a file:// URL.
	// Fall back to a synthesised path next to the FCPXML if no recording
	// path was captured (defensive; createExportFiles normally sets it).
	QString srcPath = recordingMediaPath;
	if (srcPath.isEmpty()) {
		srcPath = QFileInfo(exportFCPXMLFilePath).absolutePath() + "/" + assetName + ".mp4";
	}
	QString abs = QDir::fromNativeSeparators(QFileInfo(srcPath).absoluteFilePath());
	if (!abs.startsWith('/')) {
		abs.prepend('/');
	}
	const QString srcUrl = "file://" + abs;

	QFile file(exportFCPXMLFilePath);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
		blog(LOG_ERROR, "[StreamUP Record Chapter Manager] Failed to write FCPXML file: %s", QT_TO_UTF8(exportFCPXMLFilePath));
		return;
	}

	QTextStream out(&file);
	out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
	out << "<!DOCTYPE fcpxml>\n";
	out << "<fcpxml version=\"1.13\">\n";
	out << "  <resources>\n";
	out << "    <format id=\"r1\" name=\"FFVideoFormat\" frameDuration=\"" << fpsDen << "/" << fpsNum
	    << "s\" width=\"" << width << "\" height=\"" << height << "\"/>\n";
	out << "    <asset id=\"r2\" name=\"" << escapeXml(assetName)
	    << "\" start=\"0s\" hasVideo=\"1\" hasAudio=\"1\" videoSources=\"1\" audioSources=\"1\" duration=\""
	    << (static_cast<qint64>(durationFrames) * fpsDen) << "/" << fpsNum << "s\" format=\"r1\">\n";
	out << "      <media-rep kind=\"original-media\" src=\"" << escapeXml(srcUrl) << "\"/>\n";
	out << "    </asset>\n";
	out << "  </resources>\n";
	out << "  <library>\n";
	out << "    <event name=\"OBS Recording\">\n";
	out << "      <asset-clip name=\"" << escapeXml(assetName)
	    << "\" ref=\"r2\" offset=\"0s\" start=\"0s\" duration=\""
	    << (static_cast<qint64>(durationFrames) * fpsDen) << "/" << fpsNum << "s\" format=\"r1\">\n";
	for (const auto &m : fcpChapters) {
		out << "        <chapter-marker start=\""
		    << (static_cast<qint64>(m.first) * fpsDen) << "/" << fpsNum
		    << "s\" duration=\"" << fpsDen << "/" << fpsNum
		    << "s\" value=\"" << escapeXml(m.second) << "\"/>\n";
	}
	out << "      </asset-clip>\n";
	out << "    </event>\n";
	out << "  </library>\n";
	out << "</fcpxml>\n";
	file.close();

	blog(LOG_INFO, "[StreamUP Record Chapter Manager] Wrote FCPXML chapter file: %s (%d markers)",
	     QT_TO_UTF8(exportFCPXMLFilePath), static_cast<int>(fcpChapters.size()));

	fcpChapters.clear();
}

void ChapterMarkerDock::closePremiereXMLFile()
{
	if (!exportChaptersToPremiereXMLEnabled || exportPremiereXMLFilePath.isEmpty()) {
		return;
	}

	QFile file(exportPremiereXMLFilePath);
	if (!file.open(QIODevice::Append | QIODevice::Text)) {
		blog(LOG_ERROR, "[StreamUP Record Chapter Manager] Failed to close Premiere XML file: %s", QT_TO_UTF8(exportPremiereXMLFilePath));
		return;
	}

	QTextStream out(&file);
	out << "\t</sequence>\n";
	out << "</xmeml>\n";
	file.close();

	blog(LOG_INFO, "[StreamUP Record Chapter Manager] Closed Premiere XML file: %s", QT_TO_UTF8(exportPremiereXMLFilePath));
}

void ChapterMarkerDock::writeChapterToEDLFile(const QString &chapterName, const QString &timestamp, const QString &chapterSource)
{
	if (!exportChaptersToFileEnabled || !exportChaptersToEDLEnabled) {
		return;
	}

	if (exportEDLFilePath.isEmpty()) {
		blog(LOG_ERROR, "[StreamUP Record Chapter Manager] EDL file path is not set, creating a new file.");
		createExportFiles();
		return;
	}

	QFile file(exportEDLFilePath);
	if (!file.open(QIODevice::Append | QIODevice::Text)) {
		blog(LOG_ERROR, "[StreamUP Record Chapter Manager] Failed to open EDL file: %s", QT_TO_UTF8(exportEDLFilePath));
		return;
	}

	// Convert timestamp to timecode format (HH:MM:SS:FF)
	QString timecodeStart = convertTimestampToTimecode(timestamp, 0);
	QString timecodeEnd = convertTimestampToTimecode(timestamp, 1);

	// EDL format:
	// Event#  Reel   Track  Type  Source In  Source Out  Record In  Record Out
	// Comment line with marker info
	QTextStream out(&file);

	// Write event line
	out << QString("%1  001      V     C        %2 %3 %4 %5  \n")
		.arg(edlEventNumber, 3, 10, QChar('0'))
		.arg(timecodeStart)
		.arg(timecodeEnd)
		.arg(timecodeStart)
		.arg(timecodeEnd);

	// Write comment line with marker metadata
	QString fullChapterName = appendChapterSource(chapterName, chapterSource, addChapterSourceEnabled);

	// Use different colors based on source for visual distinction in DaVinci Resolve
	QString markerColor = "ResolveColorBlue";
	if (chapterSource.contains("Manual")) {
		markerColor = "ResolveColorGreen";
	} else if (chapterSource.contains("Scene")) {
		markerColor = "ResolveColorYellow";
	} else if (chapterSource.contains("Hotkey")) {
		markerColor = "ResolveColorPurple";
	}

	out << fullChapterName << " |C:" << markerColor << " |M:" << chapterName << " |D:1\n\n";

	file.close();

	// Increment event number for next marker
	edlEventNumber++;
}

void ChapterMarkerDock::writeAnnotationToFiles(const QString &annotationText, const QString &timestamp,
					       const QString &annotationSource)
{
	if (!obs_frontend_recording_active()) {
		setAnnotationFeedbackLabel(obs_module_text("AnnotationErrorOutputNotActive"), "error");
		return;
	}

	if (!exportChaptersToFileEnabled) {
		setAnnotationFeedbackLabel(obs_module_text("NoExportMethod"), "error");
		return;
	}

	if (annotationText.isEmpty()) {
		setAnnotationFeedbackLabel(obs_module_text("AnnotationErrorTextIsEmpty"), "error");
		return;
	}

	// Check and create export files if paths are empty
	if (exportTextFilePath.isEmpty() ||
	    (exportChaptersToFCPXMLEnabled && exportFCPXMLFilePath.isEmpty()) ||
	    (exportChaptersToPremiereXMLEnabled && exportPremiereXMLFilePath.isEmpty())) {
		createExportFiles();
	}

	// Prepare the full annotation text including the source
	QString annotationName = QString::fromUtf8(obs_module_text("Annotation"));
	QString fullAnnotationText = "(" + annotationName + ") " + annotationText + " (" + annotationSource + ")";

	// Writing to text file if enabled
	if (exportChaptersToTextEnabled) {
		if (!writeToFile(exportTextFilePath, QString("%1 - %2\n").arg(timestamp, fullAnnotationText))) {
			blog(LOG_ERROR, "[StreamUP Record Chapter Manager] Failed to open text file: %s",
			     QT_TO_UTF8(exportTextFilePath));
			return;
		}
	}

	// Write annotations as markers to FCP XML
	if (exportChaptersToFCPXMLEnabled) {
		writeChapterToFCPXMLFile(fullAnnotationText, timestamp, annotationSource);
	}

	// Write annotations as markers to Premiere XML
	if (exportChaptersToPremiereXMLEnabled) {
		writeChapterToPremiereXMLFile(fullAnnotationText, timestamp, annotationSource);
	}

	setAnnotationFeedbackLabel(obs_module_text("AnnotationSaved"), "good");
	annotationDock->annotationEdit->clear();

	// Emit WebSocket event for the new annotation
	obs_data_t *event_data = obs_data_create();
	obs_data_set_string(event_data, "annotationText", QT_TO_UTF8(annotationText));
	obs_data_set_string(event_data, "timestamp", QT_TO_UTF8(timestamp));
	obs_data_set_string(event_data, "annotationSource", QT_TO_UTF8(annotationSource));
	EmitWebSocketEvent("AnnotationSet", event_data);
	obs_data_release(event_data);
}

void ChapterMarkerDock::setAnnotationFeedbackLabel(const QString &text, const QString &themeID)
{
	annotationDock->feedbackLabel->setText(text);
	annotationDock->feedbackLabel->setProperty("themeID", themeID);
	style()->polish(annotationDock->feedbackLabel);
	annotationDock->feedbackTimer.start();
}

void ChapterMarkerDock::setChapterMarkerFeedbackLabel(const QString &text, const QString &themeID)
{
	feedbackLabel->setText(text);
	feedbackLabel->setProperty("themeID", themeID);
	style()->polish(feedbackLabel);
	feedbackTimer.start();
}

bool ChapterMarkerDock::writeToFile(const QString &filePath, const QString &content)
{
	QFile file(filePath);
	if (!file.open(QIODevice::Append | QIODevice::Text)) {
		return false;
	}

	QTextStream out(&file);
	out << content;
	file.close();
	return true;
}

//--------------------MISC EVENT HANDLERS--------------------
void ChapterMarkerDock::onSceneChanged()
{
	if (chapterOnSceneChangeEnabled && obs_frontend_recording_active()) {
		obs_source_t *current_scene = obs_frontend_get_current_scene();
		if (current_scene) {
			const char *scene_name = obs_source_get_name(current_scene);
			if (scene_name) {
				QString sceneName = QString::fromUtf8(scene_name);
				if (!ignoredScenes.contains(sceneName)) {
					addChapterMarker(sceneName, obs_module_text("ChangeScene"));
				}
			}
			obs_source_release(current_scene); // Always release regardless of scene_name
		}
	}
}

void ChapterMarkerDock::addChapterMarker(const QString &chapterName, const QString &chapterSource)
{
	QString fullChapterName = chapterName;
	QString sourceText = " (" + chapterSource + ")";

	if (addChapterSourceEnabled) {
		if (!fullChapterName.contains(sourceText)) {
			fullChapterName += sourceText;
		}
	}

	if (insertChapterMarkersInVideoEnabled) {
		bool success = obs_frontend_recording_add_chapter(QT_TO_UTF8(fullChapterName));
		if (!success) {
			blog(LOG_INFO,
			     "[StreamUP Record Chapter Manager] You have selected to insert chapters into video file. You are not using a compatible file type.");

			if (!incompatibleFileTypeMessageShown) {
				// Branded notification (was a native QMessageBox). NOTE: now
				// modeless (non-blocking) vs the old modal exec(); safe here —
				// it's a one-time notice and nothing branches on its result.
				info(this, obs_module_text("StreamUPChapterMarkerManagerError"),
				     QString("%1\n\n%2").arg(obs_module_text("IncompatibleFileTypeError"),
							     obs_module_text("IncompatibleFileType")));

				incompatibleFileTypeMessageShown = true;
			}
		}
		auto ph = obs_get_proc_handler();
		calldata cd;
		calldata_init(&cd);
		calldata_set_string(&cd, Constants::AITUM_VERTICAL_PARAM, QT_TO_UTF8(fullChapterName));
		proc_handler_call(ph, Constants::AITUM_VERTICAL_PROC, &cd);
		calldata_free(&cd);
	} // Log and handle the result of adding the chapter marker
	QString timestamp = getCurrentRecordingTime();

	// Always write to the chapter file if enabled
	if (exportChaptersToTextEnabled) {
		writeChapterToTextFile(chapterName, timestamp, chapterSource);
	}

	if (exportChaptersToFCPXMLEnabled) {
		writeChapterToFCPXMLFile(chapterName, timestamp, chapterSource);
	}

	if (exportChaptersToPremiereXMLEnabled) {
		writeChapterToPremiereXMLFile(chapterName, timestamp, chapterSource);
	}

	if (exportChaptersToEDLEnabled) {
		writeChapterToEDLFile(chapterName, timestamp, chapterSource);
	}

	blog(LOG_INFO, "[StreamUP Record Chapter Manager] Added chapter marker: %s", QT_TO_UTF8(fullChapterName));

	updateCurrentChapterLabel(fullChapterName);
	QString feedbackMessage = QString("%1 %2").arg(obs_module_text("NewChapter")).arg(fullChapterName);
	showFeedbackMessage(feedbackMessage, false);

	// Update the global current chapter name (mutex protects WebSocket thread reads)
	{
		QMutexLocker lock(&currentChapterNameMutex);
		currentChapterName = fullChapterName;
	}

	// Move the chapter to the top of the previous chapters list
	QString displayText = fullChapterName;
	if (fullChapterHistoryEnabled) {
		displayText = timestamp + " - " + fullChapterName;
	}

	QList<QListWidgetItem *> items = previousChaptersList->findItems(displayText, Qt::MatchExactly);
	if (!items.isEmpty()) {
		delete previousChaptersList->takeItem(previousChaptersList->row(items.first()));
	}
	previousChaptersList->insertItem(0, displayText);

	chapters.insert(0, chapterName);
	timestamps.insert(0, timestamp);

	// Emit WebSocket event for the new chapter marker
	obs_data_t *event_data = obs_data_create();
	obs_data_set_string(event_data, "chapterName", QT_TO_UTF8(chapterName));
	obs_data_set_string(event_data, "chapterSource", QT_TO_UTF8(chapterSource));
	EmitWebSocketEvent("ChapterMarkerSet", event_data);
	obs_data_release(event_data);

	// After the first run, set the flag to false
	isFirstRunInRecording = false;
}

void ChapterMarkerDock::onAddChapterMarker(const QString &chapterName, const QString &chapterSource)
{
	addChapterMarker(chapterName, chapterSource);
}

void ChapterMarkerDock::onAddAnnotation(const QString &annotationText, const QString &annotationSource)
{
	writeAnnotationToFiles(annotationText, getCurrentRecordingTime(), annotationSource);
}

int ChapterMarkerDock::getFrameRate() const
{
	obs_video_info ovi;
	if (obs_get_video_info(&ovi) && ovi.fps_den > 0) {
		return static_cast<int>(round(static_cast<double>(ovi.fps_num) / ovi.fps_den));
	}
	return Constants::DEFAULT_FPS;
}

//--------------------UTILITY FUNCTIONS--------------------
void ChapterMarkerDock::applyThemeIDToButton(QPushButton *button, const QString &themeID)
{
	if (!button) {
		return;
	}

	button->setProperty("themeID", themeID);
	button->setMinimumSize(S(Constants::BUTTON_MIN_WIDTH), S(Constants::BUTTON_MIN_HEIGHT));
	button->setMaximumSize(S(Constants::BUTTON_MAX_WIDTH), S(Constants::BUTTON_MAX_HEIGHT));
	button->setIconSize(QSize(S(Constants::BUTTON_ICON_SIZE), S(Constants::BUTTON_ICON_SIZE)));
	button->style()->unpolish(button);
	button->style()->polish(button);
}

QString ChapterMarkerDock::getChapterName() const
{
	return chapterNameInput->text();
}

void ChapterMarkerDock::resetRecordingStartFrameCount()
{
	// Store the current frame count when recording starts
	// This allows us to calculate timestamps relative to the recording start
	recordingStartFrameCount = obs_get_total_frames();
	blog(LOG_INFO, "[StreamUP Record Chapter Manager] Recording started at frame: %llu", (unsigned long long)recordingStartFrameCount);
}

QString ChapterMarkerDock::getCurrentRecordingTime() const
{
	// Use obs_get_total_frames() to get the live capture frame count
	// instead of obs_output_get_total_frames() which returns the encoder's
	// output frame count (which lags 1-2 seconds behind due to buffering)
	const uint64_t currentFrames = obs_get_total_frames();

	// Calculate frames elapsed since recording started
	const uint64_t framesElapsed = currentFrames - recordingStartFrameCount;

	obs_video_info ovi;
	QString recordingTimeString = Constants::DEFAULT_TIMESTAMP;

	if (obs_get_video_info(&ovi)) {
		const double frameRate = static_cast<double>(ovi.fps_num) / ovi.fps_den;
		if (frameRate > 0.0) {
			const uint64_t totalSeconds = static_cast<uint64_t>(framesElapsed / frameRate);
			// Use manual calculation instead of QTime to support recordings longer than 24 hours
			const int hours = static_cast<int>(totalSeconds / 3600);
			const int minutes = static_cast<int>((totalSeconds % 3600) / 60);
			const int seconds = static_cast<int>(totalSeconds % 60);
			recordingTimeString = QString("%1:%2:%3")
						      .arg(hours, 2, 10, QChar('0'))
						      .arg(minutes, 2, 10, QChar('0'))
						      .arg(seconds, 2, 10, QChar('0'));
		}
	}

	return recordingTimeString;
}

//--------------------CONFIGS--------------------
void ChapterMarkerDock::LoadSettings(obs_data_t *settings)
{
	// Default chapter name (fall back to constant if empty/missing)
	const char *savedName = obs_data_get_string(settings, "defaultChapterName");
	defaultChapterName = (savedName && *savedName) ? QString::fromUtf8(savedName)
						       : QString::fromUtf8(Constants::DEFAULT_CHAPTER_NAME);
	useIncrementalChapterNames = obs_data_get_bool(settings, "useIncrementalChapterNames");

	// Set chapter on scene change
	chapterOnSceneChangeEnabled = obs_data_get_bool(settings, "chapterOnSceneChangeEnabled");

	// Previous chapters
	showPreviousChaptersEnabled = obs_data_get_bool(settings, "showPreviousChaptersEnabled");
	fullChapterHistoryEnabled = obs_data_get_bool(settings, "fullChapterHistoryEnabled");

	// Write to files
	exportChaptersToFileEnabled = obs_data_get_bool(settings, "exportChaptersToFileEnabled");
	exportChaptersToTextEnabled = obs_data_get_bool(settings, "exportChaptersToTextEnabled");
	exportChaptersToFCPXMLEnabled = obs_data_get_bool(settings, "exportChaptersToFCPXmlEnabled");
	exportChaptersToPremiereXMLEnabled = obs_data_get_bool(settings, "exportChaptersToPremiereXmlEnabled");
	exportChaptersToEDLEnabled = obs_data_get_bool(settings, "exportChaptersToEDLEnabled");

	// Write chapters to video
	insertChapterMarkersInVideoEnabled = obs_data_get_bool(settings, "insertChapterMarkersInVideoEnabled");

	// Add chapter source
	addChapterSourceEnabled = obs_data_get_bool(settings, "addChapterSourceEnabled");

	// Load ignored scenes
	ignoredScenes.clear();
	obs_data_array_t *ignoredScenesArray = obs_data_get_array(settings, "ignoredScenes");
	if (ignoredScenesArray) {
		size_t count = obs_data_array_count(ignoredScenesArray);
		for (size_t i = 0; i < count; ++i) {
			obs_data_t *sceneData = obs_data_array_item(ignoredScenesArray, i);
			const char *sceneName = obs_data_get_string(sceneData, "sceneName");
			if (sceneName) {
				ignoredScenes << QString::fromUtf8(sceneName);
			}
			obs_data_release(sceneData);
		}
		obs_data_array_release(ignoredScenesArray);
	}
}

void ChapterMarkerDock::SaveSettings()
{
	obs_data_t *settings = obs_data_create();

	// Default chapter name
	obs_data_set_string(settings, "defaultChapterName",
			    defaultChapterNameEdit->text().isEmpty()
				    ? obs_module_text("Chapter")
				    : QT_TO_UTF8(defaultChapterNameEdit->text()));
	obs_data_set_bool(settings, "useIncrementalChapterNames", useIncrementalChapterNames);

	// Set chapter on scene change
	obs_data_set_bool(settings, "chapterOnSceneChangeEnabled", chapterOnSceneChangeCheckbox->isChecked());

	// Previous chapters
	obs_data_set_bool(settings, "showPreviousChaptersEnabled", showPreviousChaptersCheckbox->isChecked());
	obs_data_set_bool(settings, "fullChapterHistoryEnabled", fullChapterHistoryCheckbox->isChecked());

	// Write to files
	obs_data_set_bool(settings, "exportChaptersToFileEnabled", exportChaptersToFileCheckbox->isChecked());
	obs_data_set_bool(settings, "exportChaptersToTextEnabled", exportChaptersToTextCheckbox->isChecked());
	obs_data_set_bool(settings, "exportChaptersToFCPXmlEnabled", exportChaptersToFCPXMLCheckbox->isChecked());
	obs_data_set_bool(settings, "exportChaptersToPremiereXmlEnabled", exportChaptersToPremiereXMLCheckbox->isChecked());
	obs_data_set_bool(settings, "exportChaptersToEDLEnabled", exportChaptersToEDLCheckbox->isChecked());

	// Write chapters to video
	obs_data_set_bool(settings, "insertChapterMarkersInVideoEnabled", insertChapterMarkersCheckbox->isChecked());

	// Add chapter source
	obs_data_set_bool(settings, "addChapterSourceEnabled", addChapterSourceCheckbox->isChecked());

	// Save ignored scenes
	obs_data_array_t *ignoredScenesArray = obs_data_array_create();
	for (const QString &sceneName : ignoredScenes) {
		obs_data_t *sceneData = obs_data_create();
		obs_data_set_string(sceneData, "sceneName", QT_TO_UTF8(sceneName));
		obs_data_array_push_back(ignoredScenesArray, sceneData);
		obs_data_release(sceneData);
	}
	obs_data_set_array(settings, "ignoredScenes", ignoredScenesArray);
	obs_data_array_release(ignoredScenesArray);

	SaveLoadSettingsCallback(settings, true);

	obs_data_release(settings);
}
