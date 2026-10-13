// Layout checks: open each popup, dialog and window and fail if anything
// visible in it pokes out past its edges. Unit tests can't see this; it is how
// "the repeat editor spills out of Quick Entry" gets caught.

#include "harness.h"
#include "../testregistry.h"

using namespace e2e;

namespace {

#define TRY_ITEM(var, expr) \
    QQuickItem *var = nullptr; \
    QTRY_VERIFY_WITH_TIMEOUT((var = (expr)) != nullptr, 5000)

// Fails with the offenders listed, one per line.
#define VERIFY_NO_OVERFLOW(what, list) \
    QVERIFY2((list).isEmpty(), qPrintable(QString(what) + " has items outside its bounds:\n  " + (list).join("\n  ")))

// A window's own content against the window.
QStringList windowOverflows(QQuickWindow *w) {
    QQuickItem *root = w->contentItem();
    QStringList out = overflows(root, root);
    return out;
}

// A popup's content against the popup surface, plus the popup against its window.
QStringList popupOverflows(QObject *popup) {
    QQuickItem *surface = popupSurface(popup);
    if (!surface)
        return {"popup has no surface"};
    QStringList out = overflows(surface, surface);
    QQuickWindow *w = surface->window();
    const QRectF r = surface->mapRectToScene(QRectF(0, 0, surface->width(), surface->height()));
    if (r.left() < -1.5 || r.top() < -1.5 || r.right() > w->width() + 1.5 || r.bottom() > w->height() + 1.5)
        out << QString("popup itself at (%1,%2 %3x%4) extends past its window (%5x%6)")
                   .arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height()).arg(w->width()).arg(w->height());
    return out;
}


}  // namespace

class E2ELayout : public QObject {
    Q_OBJECT

private slots:
    // "What's in the Window" (quick-entry.md): the window with a title that
    // has been parsed into capsules, then the compact custom repeat editor
    // (Repeat > Custom..., weekly) with its weekday buttons.
    void quickEntryFitsItsWindow() {
        App app;
        QVERIFY(app.ok);
        app.press(Qt::Key_2, Qt::ShiftModifier | Qt::ControlModifier);
        QQuickWindow *quick = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((quick = windowTitled("Quick Entry")) != nullptr, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(quick->isExposed(), 5000);
        quick->requestActivate();
        TRY_ITEM(field, findItem("quickTitle"));
        click(field);
        typeInto(quick, "Collect the parcel tomorrow at 3pm #errands !!");
        QTest::qWait(150);
        QStringList plain = windowOverflows(quick);
        VERIFY_NO_OVERFLOW("Quick Entry", plain);
        // The checker itself must see an overflow when there is one.
        QVERIFY2(!overflows(field, quick->contentItem()).isEmpty(), "overflow check is blind");

        click(findItem("quickRepeat"));
        TRY_ITEM(custom, findText("Custom…", "QQuickMenuItem"));
        click(custom);
        TRY_ITEM(weeks, findItem("quickUnit:weeks"));
        click(weeks);
        TRY_ITEM(monday, findItem("quickWeekday:0"));
        QVERIFY(monday->isVisible());
        QTest::qWait(150);
        QStringList withRepeat = windowOverflows(quick);
        VERIFY_NO_OVERFLOW("Quick Entry with the custom repeat editor", withRepeat);
    }

    // "Repeating Tasks" (repeating-tasks.md): the editor card with the repeat
    // popup open, weekly so the weekday toggles show.
    void editorCardAndRepeatPopupFit() {
        App app;
        QVERIFY(app.ok);
        QVERIFY(app.openSource("list:" + QString::number(app.ids.home)));
        TRY_ITEM(row, app.row(app.ids.buyMilk));
        doubleClick(row, pointIn(row, 0.4, 0.5));
        TRY_ITEM(card, findItem("editorCard"));
        TRY_ITEM(list, findItem("taskList"));
        QStringList cardIssues = overflows(list, card);
        VERIFY_NO_OVERFLOW("Editor card", cardIssues);

        click(findItem("editorRepeat"));
        QObject *popup = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((popup = findObject("repeatEditor")) != nullptr, 5000);
        QTRY_VERIFY2_WITH_TIMEOUT(popup->property("opened").toBool(), "repeat popup did not open", 5000);
        TRY_ITEM(weekly, findText("Weekly"));
        click(weekly);
        QTest::qWait(200);
        QStringList issues = popupOverflows(popup);
        VERIFY_NO_OVERFLOW("Repeat editor popup (weekly)", issues);
    }

    // "Settings" (settings.md): every pane, none of them wider or taller than
    // the window except inside their scrolling area.
    void settingsPanesFit() {
        App app;
        QVERIFY(app.ok);
        app.press(Qt::Key_Comma, Qt::ControlModifier);
        QQuickWindow *settings = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((settings = windowTitled("Settings")) != nullptr, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(settings->isExposed(), 5000);
        const QStringList panes = settings->property("panes").toStringList();
        QVERIFY(panes.size() >= 8);
        QStringList failures;
        for (int i = 0; i < panes.size(); ++i) {
            settings->setProperty("pane", i);
            QTest::qWait(200);
            const QStringList issues = windowOverflows(settings);
            for (const QString &issue : issues)
                failures << panes[i] + ": " + issue;
        }
        VERIFY_NO_OVERFLOW("Settings", failures);
    }

    // "Import and Export" (import-and-export.md): the Export dialog.
    void exportDialogFits() {
        App app;
        QVERIFY(app.ok);
        app.press(Qt::Key_E, Qt::ShiftModifier | Qt::ControlModifier);
        QObject *dialog = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((dialog = findObject("exportDialog")) != nullptr && dialog->property("opened").toBool(), 5000);
        QTest::qWait(200);
        QStringList issues = popupOverflows(dialog);
        VERIFY_NO_OVERFLOW("Export dialog", issues);
    }

    // "Right-clicking a list... Edit List" (lists-and-list-groups.md).
    void editListDialogFits() {
        App app;
        QVERIFY(app.ok);
        TRY_ITEM(homeRow, app.sidebarRow("list:" + QString::number(app.ids.home)));
        click(homeRow, Qt::RightButton);
        TRY_ITEM(edit, findText("Edit List…", "QQuickMenuItem"));
        click(edit);
        QObject *dialog = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((dialog = findObject("editListDialog")) != nullptr && dialog->property("opened").toBool(), 5000);
        QTest::qWait(200);
        QStringList issues = popupOverflows(dialog);
        VERIFY_NO_OVERFLOW("Edit List dialog", issues);
    }

    // "Find Options" (search-and-jump-bars.md, ⌥⇧⌘F): the Search Options popup.
    void searchOptionsFit() {
        App app;
        QVERIFY(app.ok);
        app.press(Qt::Key_F, Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier);
        QObject *popup = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((popup = findObject("searchBuilder")) != nullptr && popup->property("opened").toBool(), 5000);
        QTest::qWait(200);
        QStringList issues = popupOverflows(popup);
        VERIFY_NO_OVERFLOW("Search Options", issues);
    }
};

REGISTER_TEST(E2ELayout)

#include "test_layout.moc"
