#pragma once

#include <obs-frontend-api.h>
#include <QDialog>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMap>
#include <QPushButton>
#include <QPair>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>
#include <QVector>

#include <streamup/ui/window-chrome.hpp> // ShadowDialog, applyChrome, RoundedContainer, DragFilter
#include <streamup/ui/switch-button.hpp> // SwitchButton, CreateStyledSwitch

// Forward declaration of AnnotationDock
class AnnotationDock;

class ChapterMarkerDock : public QFrame {
	Q_OBJECT

public:
	ChapterMarkerDock(QWidget *parent = nullptr);
	~ChapterMarkerDock();
	AnnotationDock *annotationDock;

	QString getChapterName() const;
	void setExportTextFilePath(const QString &filePath);
	void setExportFCPXMLFilePath(const QString &filePath);
	void setExportPremiereXMLFilePath(const QString &filePath);
	void writeChapterToFCPXMLFile(const QString &chapterName, const QString &timestamp, const QString &chapterSource);
	void writeChapterToPremiereXMLFile(const QString &chapterName, const QString &timestamp, const QString &chapterSource);
	void closeFCPXMLFile();
	void closePremiereXMLFile();

	QString getDefaultChapterName() const { return defaultChapterName; }

	QString getCurrentRecordingTime() const;
	int getFrameRate() const;
	void updateCurrentChapterLabel(const QString &chapterName);
	void createExportFiles();
	void showFeedbackMessage(const QString &message, bool isError);
	void clearPreviousChaptersGroup();
	void writeChapterToTextFile(const QString &chapterName, const QString &timestamp, const QString &chapterSource);
	void writeChapterToEDLFile(const QString &chapterName, const QString &timestamp, const QString &chapterSource);
	void setExportEDLFilePath(const QString &filePath);
	QString convertTimestampToTimecode(const QString &timestamp, int frameNumber) const;
	void addChapterMarker(const QString &chapterName, const QString &chapterSource);
	bool exportChaptersToTextEnabled;
	bool exportChaptersToFCPXMLEnabled;
	bool exportChaptersToPremiereXMLEnabled;
	bool exportChaptersToEDLEnabled;
	bool exportChaptersToFileEnabled;
	bool insertChapterMarkersInVideoEnabled;
	QString exportTextFilePath;
	QString exportFCPXMLFilePath;
	QString exportPremiereXMLFilePath;
	QString exportEDLFilePath;
	QString recordingMediaPath;
	QString recordingMediaBaseName;
	QVector<QPair<int, QString>> fcpChapters;
	QString defaultChapterName;
	int edlEventNumber;
	int fcpMarkerID;
	QStringList ignoredScenes;
	bool chapterOnSceneChangeEnabled;
	bool showPreviousChaptersEnabled;
	bool fullChapterHistoryEnabled;
	bool addChapterSourceEnabled;
	int chapterCount;
	bool useIncrementalChapterNames;

	void setAnnotationDock(AnnotationDock *dock);
	void writeAnnotationToFiles(const QString &chapterName, const QString &timestamp, const QString &chapterSource);
	void applyThemeIDToButton(QPushButton *button, const QString &themeID);
	QDialog *settingsDialog;
	void LoadSettings(obs_data_t *settings);
	void SaveSettings();
	bool isFirstRunInRecording;
	void SaveChapterHotkeys(obs_data_t *settings);
	void LoadChapterHotkeys(obs_data_t *settings);
	void LoadPresetChapters(obs_data_t *settings);
	QStringList presetChapters;
	QMap<QString, obs_hotkey_id> chapterHotkeys;
	void onAddChapterMarker(const QString &chapterName, const QString &chapterSource);
	void onAddAnnotation(const QString &annotationText, const QString &annotationSource);
	void resetRecordingStartFrameCount(); // Reset the frame count when recording starts
	uint64_t recordingStartFrameCount; // Track the frame count when recording started

signals:
	void addChapterMarkerSignal(const QString &chapterName, const QString &chapterSource);
	void addAnnotationSignal(const QString &annotationText, const QString &annotationSource);

public slots:
	void onAddChapterMarkerButton();
	void onSettingsClicked();
	void onAnnotationClicked(bool refresh = false);
	void loadAnnotationDock();
	void onSceneChanged();
	void onRecordingStopped();
	void onPreviousChapterSelected();
	void onPreviousChapterDoubleClicked(QListWidgetItem *item);
	void saveSettingsAndCloseDialog();
	void refreshMainDockUI();
	void onSetPresetChaptersButtonClicked();
	void onSetIgnoredScenesClicked();

private:
	void setupPresetChaptersDialog();
	void registerChapterHotkey(const QString &chapterName);
	void unregisterChapterHotkey(const QString &chapterName);
	QDialog *presetChaptersDialog;
	QLineEdit *presetChapterNameInput;
	QPushButton *addChapterButton;
	QPushButton *removeChapterButton;
	QListWidget *chaptersListWidget;
	void setupMainDockUI();
	void setupMainDockCurrentChapterLayout(QVBoxLayout *mainLayout);
	void setupMainDockChapterInput(QVBoxLayout *mainLayout);
	void setupMainDockSaveButtonLayout(QVBoxLayout *mainLayout);
	void setupMainDockFeedbackLabel(QVBoxLayout *mainLayout);
	void setupMainDockPreviousChaptersGroup(QVBoxLayout *mainLayout);
	void setupConnections();
	void setupOBSCallbacks();
	QDialog *createSettingsUI();
	void setupSettingsGeneralGroup(QVBoxLayout *mainLayout);
	void setupSettingsAutoChapterGroup(QVBoxLayout *mainLayout);
	void saveIgnoredScenes();
	void populateIgnoredScenesListWidget();
	StreamUP::UIStyles::SwitchButton *exportChaptersToFileCheckbox;
	StreamUP::UIStyles::SwitchButton *exportChaptersToTextCheckbox;
	StreamUP::UIStyles::SwitchButton *exportChaptersToFCPXMLCheckbox;
	StreamUP::UIStyles::SwitchButton *exportChaptersToPremiereXMLCheckbox;
	StreamUP::UIStyles::SwitchButton *exportChaptersToEDLCheckbox;
	QWidget *exportSettingsGroup;
	StreamUP::UIStyles::SwitchButton *insertChapterMarkersCheckbox;
	void setupSettingsExportGroup(QVBoxLayout *mainLayout);
	void onExportChaptersToFileToggled(bool checked);
	void onChapterOnSceneChangeToggled(bool checked);

	void setAnnotationFeedbackLabel(const QString &text, const QString &themeID);
	bool writeToFile(const QString &filePath, const QString &content);
	void setChapterMarkerFeedbackLabel(const QString &text, const QString &themeID);

	QDialog *createIgnoredScenesUI();
	QDialog *ignoredScenesDialog;
	QWidget *sceneChangeSettingsGroup;
	QLineEdit *chapterNameInput;

	QPushButton *settingsButton;
	QPushButton *setIgnoredScenesButton;
	QPushButton *annotationButton;
	QPushButton *setPresetChaptersButton;

	QLabel *currentChapterTextLabel;
	QLabel *currentChapterNameLabel;
	QLabel *feedbackLabel;
	QTimer feedbackTimer;
	QListWidget *previousChaptersList;
	QWidget *previousChaptersGroup;

	QPushButton *saveChapterMarkerButton;

	QLineEdit *defaultChapterNameEdit;
	StreamUP::UIStyles::SwitchButton *showPreviousChaptersCheckbox;
	StreamUP::UIStyles::SwitchButton *fullChapterHistoryCheckbox;
	StreamUP::UIStyles::SwitchButton *addChapterSourceCheckbox;
	StreamUP::UIStyles::SwitchButton *chapterOnSceneChangeCheckbox;
	QListWidget *ignoredScenesListWidget;

	QStringList chapters;
	QStringList timestamps;

	QWidget *exportSubWidget; // indented sub-options under "Export to file" (one aligned column)
	QVBoxLayout *exportSettingsLayout;
	bool incompatibleFileTypeMessageShown = false;
};
