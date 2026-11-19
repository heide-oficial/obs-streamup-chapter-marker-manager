// Keyboard behavior of the actual picker dialog, 2026-11-19. GPL-2.0.
#include "../chapter-picker-dialog.hpp"
#include <QDialogButtonBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <cstdlib>
#include <iostream>

static void require(bool condition, const char *message)
{
	if (!condition) {
		std::cerr << message << '\n';
		std::exit(1);
	}
}

// Deliver real Qt events without depending on QtTest, which OBS's SDK excludes.
static void keyClick(QWidget *target, Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
	QKeyEvent press(QEvent::KeyPress, key, modifiers);
	QApplication::sendEvent(target, &press);
	QKeyEvent release(QEvent::KeyRelease, key, modifiers);
	QApplication::sendEvent(target, &release);
	QApplication::processEvents();
}

int main(int argc, char **argv)
{
	QApplication app(argc, argv);
	ChapterPickerDialog picker;
	auto *layout = new QVBoxLayout(&picker);
	auto *list = new QListWidget(&picker);
	picker.choices = list;
	layout->addWidget(list);
	for (int i = 0; i < 11; ++i)
		list->addItem(QString("Chapter type %1").arg(i + 1));
	auto *button = new QPushButton("Create chapter", &picker);
	layout->addWidget(button);
	int created = 0;
	int selected = -1;
	picker.createSelected = [&]() { ++created; selected = list->currentRow(); };
	picker.enableNumberKeys();
	picker.show();
	app.processEvents();
	for (int i = 0; i < 10; ++i) {
		const auto key = static_cast<Qt::Key>(i == 9 ? Qt::Key_0 : Qt::Key_1 + i);
		const int before = created;
		keyClick(list, key);
		require(created == before + 1 && selected == i, "Number-row digit did not create the matching type");
		keyClick(list, key, Qt::KeypadModifier);
		require(created == before + 2 && selected == i, "Numpad digit did not create the matching type");
	}
	keyClick(button, Qt::Key_3);
	require(selected == 2, "Number shortcuts did not work while a footer button had focus");
	const int before = created;
	keyClick(list, Qt::Key_4, Qt::ControlModifier);
	keyClick(list, Qt::Key_4, Qt::AltModifier);
	QKeyEvent repeat(QEvent::KeyPress, Qt::Key_4, Qt::NoModifier, "4", true);
	QApplication::sendEvent(list, &repeat);
	require(created == before, "Modified keys or autorepeat created an unwanted chapter");
	keyClick(list, Qt::Key_End, Qt::KeypadModifier);
	require(selected == 0, "Num Lock off was not supported");
	keyClick(list, Qt::Key_Insert, Qt::KeypadModifier);
	require(selected == 9, "Numpad zero with Num Lock off was not supported");
	list->clear();
	list->addItem("Only chapter");
	const int beforeMissing = created;
	keyClick(list, Qt::Key_0);
	require(created == beforeMissing && picker.isVisible(), "An unassigned digit created a chapter or closed the picker");
	keyClick(list, Qt::Key_Escape);
	require(!picker.isVisible() && created == beforeMissing, "Escape did not cancel without creating a chapter");
	picker.show();
	picker.createSelected = [&]() { ++created; picker.accept(); };
	keyClick(list, Qt::Key_1);
	require(!picker.isVisible() && created == beforeMissing + 1, "A successful number selection did not close the picker");
	std::cout << "Real Qt picker: number row, numpad, focus, repeat, invalid digits and cancellation passed.\n";
}
