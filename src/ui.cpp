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
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
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
    // Top bar: status + disconnect toggle
    auto *bar = new QHBoxLayout();
    auto *statusLbl = new QLabel("Status: checking...", &window);
    auto *toggleBtn = new QPushButton("Disconnect", &window);
    bar->addWidget(statusLbl);
    bar->addStretch();
    bar->addWidget(toggleBtn);
    layout->addLayout(bar);

    auto *edit = new QTextEdit(&window);
    edit->setPlainText(readAllText(qpath));
    layout->addWidget(edit);

    // Debounced save + sync on changes
    auto *timer = new QTimer(&window);
    timer->setSingleShot(true);
    QObject::connect(edit, &QTextEdit::textChanged, [&]()
                     {
                         timer->start(300); // debounce 300ms
                     });
    QObject::connect(timer, &QTimer::timeout, [&]()
                     {
        const QString text = edit->toPlainText();
        if (!Connection::isNetworkPaused()) {
            writeAllText(qpath, text);
            Connection::updateNotepadContent(relpath, text.toStdString());
        } });

    window.resize(800, 600);
    window.show();

    // Connectivity updater
    auto *netTimer = new QTimer(&window);
    QObject::connect(netTimer, &QTimer::timeout, [&]()
                     {
        bool paused = Connection::isNetworkPaused();
        // If we had a real internet check, we'd OR with actual reachability
        statusLbl->setText(paused ? "Status: disconnected" : "Status: connected");
        toggleBtn->setText(paused ? "Connect" : "Disconnect"); });
    netTimer->start(500);

    // Toggle simulated network connectivity
    QObject::connect(toggleBtn, &QPushButton::clicked, [&]()
                     {
        bool now = !Connection::isNetworkPaused();
        Connection::setNetworkPaused(now);
        // When reconnected, push the latest editor buffer to disk and sync
        if (!now) {
            Connection::connectAllReaders();
            Transfer::announceOpenNotepad(relpath);
            const QString text = edit->toPlainText();
            writeAllText(qpath, text);
            Connection::updateNotepadContent(relpath, text.toStdString());
        } });

    // Periodic snapshot broadcaster to catch late reader reconnects
    auto *snapshotTimer = new QTimer(&window);
    QObject::connect(snapshotTimer, &QTimer::timeout, [&]()
                     {
        if (!Connection::isNetworkPaused()) {
            // reflect current edit buffer to disk and announce
            const QString text = edit->toPlainText();
            writeAllText(qpath, text);
            Connection::updateNotepadContent(relpath, text.toStdString());
        } });
    snapshotTimer->start(2000);

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
    // Top bar with status and toggle for testing
    auto *bar = new QHBoxLayout();
    auto *statusLbl = new QLabel("Status: checking...", &window);
    auto *toggleBtn = new QPushButton("Disconnect", &window);
    bar->addWidget(statusLbl);
    bar->addStretch();
    bar->addWidget(toggleBtn);
    layout->addLayout(bar);
    auto *edit = new QTextEdit(&window);
    edit->setReadOnly(true);
    layout->addWidget(edit);

    // Periodically refresh view from file
    auto *timer = new QTimer(&window);
    QObject::connect(timer, &QTimer::timeout, [&]()
                     { edit->setPlainText(readAllText(qpath)); });
    timer->start(300);

    // Connectivity updater
    auto *netTimer = new QTimer(&window);
    QObject::connect(netTimer, &QTimer::timeout, [&]()
                     {
        bool paused = Connection::isNetworkPaused();
        statusLbl->setText(paused ? "Status: disconnected" : "Status: connected");
        toggleBtn->setText(paused ? "Connect" : "Disconnect"); });
    netTimer->start(500);

    // Toggle simulated connectivity on reader
    QObject::connect(toggleBtn, &QPushButton::clicked, [&]()
                     {
                         bool now = !Connection::isNetworkPaused();
                         Connection::setNetworkPaused(now);
                         // Reader will receive a new snapshot from writer's periodic broadcaster
                     });

    window.resize(800, 600);
    window.show();
    app.exec();
}

#else

// Stubs when UI is disabled
void ui_start_writer(const std::string &relpath) { (void)relpath; }
void ui_start_reader(const std::string &relpath) { (void)relpath; }

#endif
