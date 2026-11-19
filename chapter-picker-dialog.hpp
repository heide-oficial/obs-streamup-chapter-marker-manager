// Modified for heide-oficial's fork, 2026-11-19. GPL-2.0; see LICENSE.
#pragma once

#include <QApplication>
#include <QDialog>
#include <QKeyEvent>
#include <QListWidget>
#include <functional>

// Reused by the plugin and the keyboard regression tests. Filtering the dialog's
// descendants also handles digits when the list or a footer button has focus.
class ChapterPickerDialog : public QDialog {
public:
	using QDialog::QDialog;
	QListWidget *choices = nullptr;
	std::function<void()> createSelected;

	~ChapterPickerDialog() override
	{
		if (qApp)
			qApp->removeEventFilter(this);
	}

	void enableNumberKeys() { qApp->installEventFilter(this); }

	static int rowForKey(int key, Qt::KeyboardModifiers modifiers)
	{
		if (modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
			return -1;
		if (key >= Qt::Key_1 && key <= Qt::Key_9)
			return key - Qt::Key_1;
		if (key == Qt::Key_0)
			return 9;
		if (modifiers & Qt::KeypadModifier) {
			// Windows reports navigation keys for a numpad with Num Lock off.
			switch (key) {
			case Qt::Key_End: return 0;
			case Qt::Key_Down: return 1;
			case Qt::Key_PageDown: return 2;
			case Qt::Key_Left: return 3;
			case Qt::Key_Clear: return 4;
			case Qt::Key_Right: return 5;
			case Qt::Key_Home: return 6;
			case Qt::Key_Up: return 7;
			case Qt::Key_PageUp: return 8;
			case Qt::Key_Insert: return 9;
			default: break;
			}
		}
		return -1;
	}

protected:
	bool eventFilter(QObject *watched, QEvent *event) override
	{
		auto *widget = qobject_cast<QWidget *>(watched);
		if (isVisible() && choices && widget && (widget == this || isAncestorOf(widget)) &&
		    (event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride)) {
			auto *key = static_cast<QKeyEvent *>(event);
			const int row = rowForKey(key->key(), key->modifiers());
			if (row >= 0) {
				event->accept();
				if (event->type() == QEvent::KeyPress && !key->isAutoRepeat() && row < choices->count()) {
					choices->setCurrentRow(row);
					if (createSelected)
						createSelected();
				}
				return true;
			}
		}
		return QDialog::eventFilter(watched, event);
	}
};
