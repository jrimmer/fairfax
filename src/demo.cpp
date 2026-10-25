// Demo data for `fairfax --demo`: a lived-in database shaped for
// screenshots, with dates relative to today so it always looks current.

#include "backend.h"

#include <QFile>
#include <QFileInfo>

namespace {

struct Seeder {
    Store *store;
    QDate today = QDate::currentDate();

    qint64 group(const QString &name, double order) {
        ListGroup g;
        g.id = store->nextId("list_groups");
        g.name = name;
        g.order = order;
        store->putGroup(g);
        return g.id;
    }

    qint64 list(const QString &name, qint64 groupId, int color, double order) {
        TaskList l;
        l.id = store->nextId("lists");
        l.name = name;
        l.groupId = groupId;
        l.color = color;
        l.order = order;
        store->putList(l);
        return l.id;
    }

    qint64 tagGroup(const QString &name, double order) {
        TagGroup g;
        g.id = store->nextId("tag_groups");
        g.name = name;
        g.order = order;
        store->putTagGroup(g);
        return g.id;
    }

    QHash<QString, qint64> tags;
    void tag(const QString &name, qint64 groupId) {
        Tag t;
        t.id = store->nextId("tags");
        t.name = name;
        t.groupId = groupId;
        store->putTag(t);
        tags.insert(name, t.id);
    }

    qint64 task(qint64 listId, const QString &title, int dueDays, int dueMinutes, int duration,
                const QStringList &tagNames = {}, int priority = 0, qint64 parent = 0, int type = TaskType::Task) {
        Task t;
        t.id = store->nextId("tasks");
        t.listId = listId;
        t.parentId = parent;
        t.type = type;
        t.title = title;
        if (dueDays != INT_MIN)
            t.dueDate = today.addDays(dueDays);
        t.dueTime = dueMinutes;
        t.duration = duration;
        t.priority = priority;
        t.order = t.id;
        for (const auto &name : tagNames)
            t.tagIds.append(tags.value(name));
        store->putTask(t);
        return t.id;
    }

    void edit(qint64 id, const std::function<void(Task &)> &change) {
        Task t = store->task(id);
        change(t);
        store->putTask(t);
    }
};

constexpr int NoDate = INT_MIN;

}  // namespace

void Backend::seedDemo() {
    if (!m_store->tasks().isEmpty())
        return;
    Seeder s{m_store};
    m_store->begin("Demo");

    const qint64 personal = s.group("Personal", 1);
    const qint64 work = s.group("Work", 2);
    const qint64 home = s.list("Home", personal, 0, 1);
    const qint64 family = s.list("Family", personal, 1, 2);
    const qint64 errands = s.list("Errands", personal, 7, 3);
    const qint64 office = s.list("Work", work, 2, 4);
    const qint64 launch = s.list("Product Launch", work, 3, 5);

    for (const auto &name : {"impulse", "farm", "cantsleep"})
        s.tag(name, 0);
    const qint64 workTags = s.tagGroup("Work", 1);
    for (const auto &name : {"work", "design", "email", "call"})
        s.tag(name, workTags);
    const qint64 homeTags = s.tagGroup("Home", 2);
    for (const auto &name : {"home", "errands", "family", "garden"})
        s.tag(name, homeTags);
    const qint64 travelTags = s.tagGroup("Travel", 3);
    for (const auto &name : {"travel", "flight", "hotel", "packing"})
        s.tag(name, travelTags);

    s.task(office, "Submit Q2 budget notes", -4, -1, 30, {"work"}, 3);
    const qint64 bill = s.task(home, "Pay electricity bill", -3, -1, 15, {"home"}, 3);
    s.edit(bill, [](Task &t) { t.recurrence.frequency = Repeat::Monthly; });

    const qint64 video = s.task(launch, "Product launch video", 6, -1, 0, {"design"}, 2, 0, TaskType::Project);
    const qint64 demoScript = s.task(launch, "Call Alex about demo script", -1, -1, 20, {"call"}, 3, video);
    s.edit(demoScript, [](Task &t) {
        t.actionType = ActionType::Call;
        t.actionValue = "+1 555 0142";
    });
    s.task(launch, "Draft storyboard for key features", 0, 14 * 60, 90, {"design"}, 0, video);
    s.task(launch, "Book the studio", 4, -1, 30, {"work"}, 1, video);

    const qint64 mobility = s.task(home, "Daily mobility routine", 0, 7 * 60, 25, {"home"});
    s.edit(mobility, [](Task &t) { t.recurrence.frequency = Repeat::Daily; });
    const qint64 birthday = s.task(family, "Call Alex about birthday dinner", 0, 10 * 60 + 30, 0, {"family", "call"}, 2);
    s.edit(birthday, [](Task &t) {
        t.actionType = ActionType::Call;
        t.actionValue = "+1 555 0199";
    });
    const qint64 review = s.task(office, "Weekly review and plan next sprint", 0, 16 * 60, 45, {"work"});
    s.edit(review, [](Task &t) { t.recurrence.frequency = Repeat::Weekly; });
    const qint64 grocery = s.task(errands, "Grocery run", 0, 17 * 60 + 15, 60, {"errands"}, 0, 0, TaskType::Checklist);
    for (const auto &item : {"Oat milk", "Coffee beans", "Lemons", "Sourdough"})
        s.task(errands, item, NoDate, -1, 0, {}, 0, grocery);
    s.task(home, "Clean kitchen pantry", 0, 18 * 60 + 30, 45, {"home"});

    const qint64 forms = s.task(family, "Email Sam school forms", 1, 9 * 60, 0, {"email", "family"});
    s.edit(forms, [](Task &t) {
        t.actionType = ActionType::Mail;
        t.actionValue = "sam@example.com";
        t.notes = "Permission slip and the photo consent form.\nDeadline is Friday at the front office.";
    });
    s.task(office, "Book DXB to HND flight", 1, 12 * 60 + 15, 30, {"travel", "flight"}, 2);
    s.task(home, "Pick up library holds", 1, -1, 0, {"errands"});
    s.task(office, "Review onboarding mockups", 2, 11 * 60 + 30, 45, {"design"});
    s.task(office, "Quarterly planning offsite", 5, 9 * 60, 0, {"work"}, 1);
    const qint64 garden = s.task(home, "Plant the autumn bulbs", 9, -1, 120, {"garden"});
    s.edit(garden, [this](Task &t) { t.startDate = QDate::currentDate().addDays(7); });
    const qint64 passport = s.task(family, "Renew passport", 14, -1, 60, {"travel"}, 1);
    s.edit(passport, [](Task &t) {
        t.starred = true;
        t.startDate = QDate::currentDate().addDays(3);
    });
    const qint64 packing = s.task(family, "Pack for the Tokyo trip", 12, -1, 2 * 24 * 60, {"packing", "travel"}, 0,
                                  0, TaskType::Checklist);
    for (const auto &item : {"Passport", "Chargers", "Rail pass", "Gifts"})
        s.task(family, item, NoDate, -1, 0, {}, 0, packing);
    s.task(home, "Fix the squeaky gate", NoDate, -1, 30, {"home"});
    s.task(home, "Try the new ramen place", NoDate, -1, 0, {"impulse"});
    const qint64 read = s.task(office, "Read the platform RFC", NoDate, -1, 60, {"work"});
    s.edit(read, [](Task &t) { t.starred = true; });

    // Sections in the Work list, like the task sections.
    auto section = [&](const QString &name, double order) {
        ListSection sec;
        sec.id = m_store->nextId("sections");
        sec.listId = office;
        sec.name = name;
        sec.order = order;
        m_store->putSection(sec);
        return sec.id;
    };
    const qint64 sprint = section("This Sprint", 1);
    const qint64 backlog = section("Backlog", 2);
    for (auto t : m_store->tasks()) {
        if (t.listId != office || t.parentId)
            continue;
        t.sectionId = t.dueDate.isValid() && t.dueDate <= QDate::currentDate().addDays(5) ? sprint : backlog;
        m_store->putTask(t);
    }
    TaskList officeList = m_store->list(office);
    officeList.sortField = SortField::Section;
    m_store->putList(officeList);

    TaskList important;
    important.id = m_store->nextId("lists");
    important.kind = ListKind::Smart;
    important.name = "Important";
    important.smartQuery = "type: hiprio OR starred";
    important.order = 1;
    m_store->putList(important);
    TaskList calls;
    calls.id = m_store->nextId("lists");
    calls.kind = ListKind::Smart;
    calls.name = "Calls to make";
    calls.smartQuery = "type: call";
    calls.order = 2;
    m_store->putList(calls);

    for (const qint64 task : {birthday, review}) {
        Alarm a;
        a.id = m_store->nextId("alarms");
        a.taskId = task;
        a.offset = 15;
        m_store->putAlarm(a);
    }

    m_store->commit();

    // A calendar for the day bar: three events today.
    const QString ics = QFileInfo(m_store->path()).absolutePath() + "/demo-calendar.ics";
    QFile file(ics);
    if (file.open(QIODevice::WriteOnly)) {
        const QString day = QDate::currentDate().toString("yyyyMMdd");
        auto event = [&](const char *uid, const char *title, const char *start, const char *end) {
            return QStringLiteral("BEGIN:VEVENT\r\nUID:%1\r\nSUMMARY:%2\r\nDTSTART:%3T%4\r\nDTEND:%3T%5\r\nEND:VEVENT\r\n")
                .arg(uid, title, day, start, end);
        };
        file.write(("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Fairfax//Demo//EN\r\nX-WR-CALNAME:Work\r\n" +
                    event("standup", "Team stand-up", "093000", "100000") + event("jeremy", "Call with Jeremy", "110000", "114500") +
                    event("lunch", "Lunch with Ana", "123000", "133000") + event("review", "Design review", "150000", "160000") +
                    // Declined by the demo user, so the bar shows it struck through.
                    event("sync", "Vendor sync", "163000", "170000").replace("END:VEVENT", "ATTENDEE;PARTSTAT=DECLINED:mailto:me@example.com\r\nEND:VEVENT") +
                    "END:VCALENDAR\r\n").toUtf8());
        file.close();
        m_store->setSetting("calendar.sources", ics);
        m_store->setSetting("calendar.emails", "me@example.com");
    }
    rebuild();
}
