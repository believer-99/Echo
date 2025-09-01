#include "include/ui.hpp"
#include "include/connection.hpp"
#include "transfer.hpp"

#ifdef ECHO_ENABLE_UI
#include <QApplication>
#include <QMainWindow>
#include <QTextEdit>
#include <QTimer>
#include <QFile>
#include <QTextStream>
#include <QVBoxLayout>
#include <QWidget>

static QString readAllText(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    QTextStream ts(&f);
    return ts.readAll();
}

static void writeAllText(const QString &path, const QString &text)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return;
    QTextStream ts(&f);
    ts << text;
}

void ui_start_writer(const std::string &relpath)
{
    int argc = 1;
    char arg0[] = "echo";
    char *argv[] = {arg0, nullptr};
    QApplication app(argc, argv);

    const QString qpath = QString::fromStdString(relpath);

    // Initialize connection state, connect to readers, and announce open + snapshot
    Connection::setCurrentNotepad(relpath);
    Connection::connectAllReaders();
    // Tell readers to open their viewer immediately
    Transfer::announceOpenNotepad(relpath);
    Connection::announceNotepadSnapshot(relpath);

    QWidget window;
    window.setWindowTitle(QString("Writer Notepad - %1 (auto-saves & syncs)").arg(qpath));
    auto *layout = new QVBoxLayout(&window);
    auto *edit = new QTextEdit(&window);
    edit->setPlainText(readAllText(qpath));
    layout->addWidget(edit);

    // Debounced save + sync on changes
    auto *timer = new QTimer(&window);
    timer->setSingleShot(true);
    QObject::connect(edit, &QTextEdit::textChanged, [&]()
                     {
                         timer->start(200); // debounce 200ms
                     });
    QObject::connect(timer, &QTimer::timeout, [&]()
                     {
        const QString text = edit->toPlainText();
        writeAllText(qpath, text);
        Connection::updateNotepadContent(relpath, text.toStdString()); });

    window.resize(800, 600);
    window.show();
    app.exec();
}

void ui_start_reader(const std::string &relpath)
{
    int argc = 1;
    char arg0[] = "echo";
    char *argv[] = {arg0, nullptr};
    QApplication app(argc, argv);

    const QString qpath = QString::fromStdString(relpath);
    QWidget window;
    window.setWindowTitle(QString("Reader Notepad - %1 (read-only)").arg(qpath));
    auto *layout = new QVBoxLayout(&window);
    auto *edit = new QTextEdit(&window);
    edit->setReadOnly(true);
    layout->addWidget(edit);

    // Periodically refresh view from file
    auto *timer = new QTimer(&window);
    QObject::connect(timer, &QTimer::timeout, [&]()
                     { edit->setPlainText(readAllText(qpath)); });
    timer->start(300);

    window.resize(800, 600);
    window.show();
    app.exec();
}

#else

// Stubs when UI is disabled
void ui_start_writer(const std::string &relpath) { (void)relpath; }
void ui_start_reader(const std::string &relpath) { (void)relpath; }

#endif
