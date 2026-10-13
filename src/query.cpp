#include "query.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <algorithm>
#include <vector>

#include "store.h"

using NodePtr = std::shared_ptr<Query::Node>;
using Predicate = std::function<bool(const Task &, const QueryContext &)>;

struct Query::Node {
    enum Kind { And, Or, Not, Leaf } kind = Leaf;
    std::vector<NodePtr> kids;
    Predicate fn;

    bool eval(const Task &t, const QueryContext &c) const {
        switch (kind) {
        case And:
            for (const auto &k : kids)
                if (!k->eval(t, c)) return false;
            return true;
        case Or:
            for (const auto &k : kids)
                if (k->eval(t, c)) return true;
            return false;
        case Not:
            return kids.empty() ? true : !kids.front()->eval(t, c);
        case Leaf:
            return fn ? fn(t, c) : true;
        }
        return true;
    }
};

Query::Query() = default;
Query::~Query() = default;
Query::Query(const Query &) = default;
Query &Query::operator=(const Query &) = default;

namespace {

// ---------------------------------------------------------------- helpers

NodePtr leaf(Predicate fn) {
    auto n = std::make_shared<Query::Node>();
    n->kind = Query::Node::Leaf;
    n->fn = std::move(fn);
    return n;
}

NodePtr combine(Query::Node::Kind kind, const std::vector<NodePtr> &kids) {
    std::vector<NodePtr> live;
    for (const auto &k : kids)
        if (k) live.push_back(k);
    if (live.empty()) return nullptr;
    if (live.size() == 1) return live.front();
    auto n = std::make_shared<Query::Node>();
    n->kind = kind;
    n->kids = std::move(live);
    return n;
}

NodePtr makeNot(const NodePtr &kid) {
    if (!kid) return nullptr;
    auto n = std::make_shared<Query::Node>();
    n->kind = Query::Node::Not;
    n->kids.push_back(kid);
    return n;
}

QString fold(const QString &s) {
    const QString d = s.normalized(QString::NormalizationForm_D);
    QString out;
    out.reserve(d.size());
    for (const QChar c : d) {
        const auto cat = c.category();
        if (cat == QChar::Mark_NonSpacing || cat == QChar::Mark_SpacingCombining || cat == QChar::Mark_Enclosing)
            continue;
        out.append(c);
    }
    return out.toCaseFolded();
}

QStringList splitWords(const QString &folded) {
    QStringList words;
    QString cur;
    for (const QChar c : folded) {
        if (c.isLetterOrNumber()) {
            cur.append(c);
        } else if (!cur.isEmpty()) {
            words << cur;
            cur.clear();
        }
    }
    if (!cur.isEmpty()) words << cur;
    return words;
}

QString soundexCode(const QString &folded) {
    auto digit = [](QChar c) -> char {
        switch (c.unicode()) {
        case 'b': case 'f': case 'p': case 'v': return '1';
        case 'c': case 'g': case 'j': case 'k': case 'q': case 's': case 'x': case 'z': return '2';
        case 'd': case 't': return '3';
        case 'l': return '4';
        case 'm': case 'n': return '5';
        case 'r': return '6';
        default: return 0;
        }
    };
    QString letters;
    for (const QChar c : folded)
        if (c.unicode() >= 'a' && c.unicode() <= 'z') letters.append(c);
    if (letters.isEmpty()) return {};
    QString code(letters.at(0).toUpper());
    char last = digit(letters.at(0));
    for (int i = 1; i < letters.size() && code.size() < 4; ++i) {
        const QChar c = letters.at(i);
        const char d = digit(c);
        if (d) {
            if (d != last) code.append(QChar(d));
            last = d;
        } else if (c != 'h' && c != 'w') {
            last = 0;  // A vowel separates equal codes; h and w do not.
        }
    }
    while (code.size() < 4) code.append('0');
    return code;
}

// Glob match of a folded name against a folded pattern with * wildcards.
bool nameMatches(const QString &pattern, const QString &name) {
    if (!pattern.contains('*')) return pattern == name;
    QString rx = QRegularExpression::escape(pattern);
    rx.replace(QStringLiteral("\\*"), QStringLiteral(".*"));
    return QRegularExpression(QStringLiteral("^") + rx + QStringLiteral("$")).match(name).hasMatch();
}

struct Tok {
    enum K { Word, Quoted, LP, RP, Comma, Or, And, Not } k = Word;
    QString s;
};

struct TokOptions {
    bool commaIsOr = false;
    bool dashNegates = false;
    bool mergeWords = false;
};

std::vector<Tok> tokenize(const QString &text, const TokOptions &o) {
    std::vector<Tok> out;
    int i = 0;
    const int n = text.size();
    auto isStop = [&](QChar c) { return c.isSpace() || c == '(' || c == ')' || c == '"' || (o.commaIsOr && c == ','); };
    while (i < n) {
        const QChar c = text.at(i);
        if (c.isSpace()) { ++i; continue; }
        if (c == '(') { out.push_back({Tok::LP, {}}); ++i; continue; }
        if (c == ')') { out.push_back({Tok::RP, {}}); ++i; continue; }
        if (o.commaIsOr && c == ',') { out.push_back({Tok::Comma, {}}); ++i; continue; }
        if (c == '"') {
            const int end = text.indexOf('"', i + 1);
            const QString body = end < 0 ? text.mid(i + 1) : text.mid(i + 1, end - i - 1);
            out.push_back({Tok::Quoted, body});
            i = end < 0 ? n : end + 1;
            continue;
        }
        if ((c == '!' || (o.dashNegates && c == '-')) && i + 1 < n && !text.at(i + 1).isSpace()) {
            out.push_back({Tok::Not, {}});
            ++i;
            continue;
        }
        int j = i;
        while (j < n && !isStop(text.at(j))) ++j;
        if (j == i) { ++i; continue; }
        const QString w = text.mid(i, j - i);
        i = j;
        if (w.indexOf(QRegularExpression(QStringLiteral("[^-!]"))) < 0) continue;  // Stray "-", "!!" and the like.
        if (w.compare(QLatin1String("or"), Qt::CaseInsensitive) == 0) out.push_back({Tok::Or, {}});
        else if (w.compare(QLatin1String("and"), Qt::CaseInsensitive) == 0) out.push_back({Tok::And, {}});
        else if (o.mergeWords && !out.empty() && out.back().k == Tok::Word) out.back().s += ' ' + w;
        else out.push_back({Tok::Word, w});
    }
    return out;
}

// leaf(token, negated) -> node or nullptr when the token is to be ignored.
using LeafFactory = std::function<NodePtr(const Tok &, bool)>;

class ExprParser {
public:
    ExprParser(std::vector<Tok> toks, LeafFactory f, QStringList *errors)
        : m_t(std::move(toks)), m_leaf(std::move(f)), m_errors(errors) {}

    NodePtr parse() {
        std::vector<NodePtr> parts;
        while (m_p < m_t.size()) {
            parts.push_back(parseOr());
            if (m_p < m_t.size() && m_t[m_p].k == Tok::RP) {
                *m_errors << QStringLiteral("unmatched )");
                ++m_p;
            }
        }
        return combine(Query::Node::And, parts);
    }

private:
    bool at(Tok::K k) const { return m_p < m_t.size() && m_t[m_p].k == k; }

    NodePtr parseOr() {
        std::vector<NodePtr> kids{parseAnd()};
        while (at(Tok::Or) || at(Tok::Comma)) {
            ++m_p;
            kids.push_back(parseAnd());
        }
        return combine(Query::Node::Or, kids);
    }

    NodePtr parseAnd() {
        std::vector<NodePtr> kids;
        while (m_p < m_t.size()) {
            const Tok::K k = m_t[m_p].k;
            if (k == Tok::RP || k == Tok::Or || k == Tok::Comma) break;
            if (k == Tok::And) { ++m_p; continue; }
            kids.push_back(parseUnary());
        }
        return combine(Query::Node::And, kids);
    }

    NodePtr parseUnary() {
        const Tok tok = m_t[m_p++];
        switch (tok.k) {
        case Tok::Not:
            if (m_p >= m_t.size()) return nullptr;
            if (at(Tok::Word) || at(Tok::Quoted)) return m_leaf(m_t[m_p++], true);
            if (at(Tok::LP)) { ++m_p; return makeNot(parseGroup()); }
            if (at(Tok::Not)) return makeNot(parseUnary());
            return nullptr;
        case Tok::LP:
            return parseGroup();
        case Tok::Word:
        case Tok::Quoted:
            return m_leaf(tok, false);
        default:
            return nullptr;
        }
    }

    NodePtr parseGroup() {
        if (m_depth >= 64) return nullptr;  // Too deep: the contents are read flat.
        ++m_depth;
        NodePtr n = parseOr();
        if (at(Tok::RP)) ++m_p;
        else *m_errors << QStringLiteral("missing )");
        --m_depth;
        return n;
    }

    std::vector<Tok> m_t;
    LeafFactory m_leaf;
    QStringList *m_errors;
    size_t m_p = 0;
    int m_depth = 0;
};

// ------------------------------------------------------------- free text

struct Folded {
    QString key;
    QStringList fields;                 // Title, notes, then each tag.
    QList<QStringList> words;           // Words of each field.
    QStringList codes;                  // Soundex codes of title and notes words.
    bool haveCodes = false;
};

QStringList tagNamesOf(const Task &t, const QueryContext &c) {
    QStringList names;
    if (!c.store) return names;
    for (qint64 id : t.tagIds) {
        auto it = c.store->tags().constFind(id);
        if (it != c.store->tags().constEnd() && !it->deleted) names << it->name;
    }
    return names;
}

QStringList locationNamesOf(const Task &t, const QueryContext &c) {
    QStringList names;
    if (!c.store) return names;
    for (qint64 id : t.locationIds) {
        auto it = c.store->locations().constFind(id);
        if (it != c.store->locations().constEnd() && !it->deleted) names << it->name;
    }
    return names;
}

bool hasAttachment(const Task &t, const QueryContext &c, int kind) {
    if (!c.store) return false;
    for (const Attachment &a : c.store->attachmentsFor(t.id))
        if (a.kind == kind) return true;
    return false;
}

const Folded &foldedFor(const Task &t, const QueryContext &c) {
    thread_local Folded cache;
    const QStringList tags = tagNamesOf(t, c);
    const QString key = t.title + QChar(0x1f) + t.notes + QChar(0x1f) + tags.join(QChar(0x1e));
    if (cache.key == key && !cache.fields.isEmpty()) return cache;
    cache = Folded();
    cache.key = key;
    cache.fields << fold(t.title) << fold(t.notes);
    for (const QString &tag : tags) {
        const QString f = fold(tag);
        cache.fields << f << (QStringLiteral("#") + f) << (QStringLiteral("@") + f);
    }
    for (const QString &f : std::as_const(cache.fields)) cache.words << splitWords(f);
    return cache;
}

void ensureCodes(Folded &f) {
    if (f.haveCodes) return;
    f.haveCodes = true;
    for (int i = 0; i < 2 && i < f.words.size(); ++i)
        for (const QString &w : f.words.at(i)) f.codes << soundexCode(w);
}

struct TextTerm {
    QString text;
    bool lead = false, trail = false, phrase = false;
    QString code;  // Non-empty when soundex applies.
    QRegularExpression rx;  // For inner wildcards.
};

bool termMatches(const TextTerm &term, const Task &t, const QueryContext &c) {
    Folded &f = const_cast<Folded &>(foldedFor(t, c));
    if (term.text.isEmpty()) return true;  // A bare "*".
    if (term.phrase) {
        for (const QString &field : std::as_const(f.fields))
            if (field.contains(term.text)) return true;
        return false;
    }
    if (term.rx.isValid() && !term.rx.pattern().isEmpty()) {
        for (const auto &ws : std::as_const(f.words))
            for (const QString &w : ws)
                if (term.rx.match(w).hasMatch()) return true;
        return false;
    }
    const bool bare = term.lead == term.trail;
    if (bare) {
        for (const QString &field : std::as_const(f.fields))
            if (field.contains(term.text)) return true;
    } else {
        for (const auto &ws : std::as_const(f.words))
            for (const QString &w : ws)
                if (term.trail ? w.startsWith(term.text) : w.endsWith(term.text)) return true;
    }
    if (c.soundex && !term.code.isEmpty()) {
        ensureCodes(f);
        if (f.codes.contains(term.code)) return true;
    }
    return false;
}

NodePtr textLeaf(const Tok &tok, bool negated, QStringList *errors) {
    TextTerm term;
    if (tok.k == Tok::Quoted) {
        term.phrase = true;
        term.text = fold(tok.s).simplified();
        if (term.text.isEmpty()) return nullptr;
    } else {
        QString s = tok.s;
        term.lead = s.startsWith('*');
        while (s.startsWith('*')) s.remove(0, 1);
        term.trail = s.endsWith('*');
        while (s.endsWith('*')) s.chop(1);
        if (s.isEmpty() && !term.lead && !term.trail) return nullptr;
        term.text = fold(s);
        if (term.text.contains('*')) {
            QStringList parts = term.text.split('*', Qt::KeepEmptyParts);
            for (QString &p : parts) p = QRegularExpression::escape(p);
            term.rx = QRegularExpression((term.lead ? QString() : QStringLiteral("^")) + parts.join(QStringLiteral(".*")) +
                                         (term.trail ? QString() : QStringLiteral("$")));
        } else if (!term.lead && !term.trail && !s.startsWith('#') && !s.startsWith('@')) {
            bool alpha = term.text.size() >= 2;
            for (const QChar ch : term.text)
                if (!(ch.unicode() >= 'a' && ch.unicode() <= 'z')) alpha = false;
            if (alpha) term.code = soundexCode(term.text);
        }
    }
    Q_UNUSED(errors);
    NodePtr n = leaf([term](const Task &t, const QueryContext &c) { return termMatches(term, t, c); });
    return negated ? makeNot(n) : n;
}

// ------------------------------------------------------------ type: values

struct TypePreset {
    const char *name;
    Predicate fn;
};

bool dueWindowIntersects(const Task &t, const QDate &from, const QDate &to) {
    if (!t.dueDate.isValid()) return false;
    const QDateTime es = Schedule::effectiveStart(t);
    const QDate first = es.isValid() ? std::min(es.date(), t.dueDate) : t.dueDate;
    return first <= to && t.dueDate >= from;
}

bool startWithin(const Task &t, const QDate &from, const QDate &to) {
    return t.startDate.isValid() && t.startDate >= from && t.startDate <= to;
}

bool doneWithin(const Task &t, const QDate &from, const QDate &to) {
    return t.completed && t.completedAt.isValid() && t.completedAt.date() >= from && t.completedAt.date() <= to;
}

bool hasActiveChildren(const Task &t, const QueryContext &c) {
    if (!c.store) return false;
    for (auto it = c.store->tasks().constBegin(); it != c.store->tasks().constEnd(); ++it)
        if (it->parentId == t.id && !it->completed && !it->deleted) return true;
    return false;
}

const QHash<QString, Predicate> &typePresets() {
    static const QHash<QString, Predicate> map = [] {
        QHash<QString, Predicate> m;
        auto add = [&m](std::initializer_list<const char *> names, Predicate fn) {
            for (const char *n : names) m.insert(QString::fromLatin1(n), fn);
        };
        using T = const Task &;
        using C = const QueryContext &;
        auto action = [](int a) { return [a](T t, C) { return t.actionType == a; }; };
        auto day = [](C c) { return c.now.date(); };
        auto week = [](C c, QDate &a, QDate &b) { a = Schedule::weekStartOf(c.now.date(), c.settings.weekStart); b = a.addDays(6); };

        add({"alltasks"}, [](T, C) { return true; });
        add({"task"}, [](T t, C) { return t.type == TaskType::Task && t.parentId == 0; });
        add({"subtasks"}, [](T t, C) { return t.parentId != 0; });
        add({"project", "proj"}, [](T t, C) { return t.type == TaskType::Project; });
        add({"checklist", "chck"}, [](T t, C) { return t.type == TaskType::Checklist; });
        add({"emptyproj"}, [](T t, C c) { return t.type == TaskType::Project && !hasActiveChildren(t, c); });
        add({"emptychck"}, [](T t, C c) { return t.type == TaskType::Checklist && !hasActiveChildren(t, c); });

        add({"action"}, [](T t, C) { return t.actionType != ActionType::None; });
        add({"call"}, action(ActionType::Call));
        add({"sms", "message"}, action(ActionType::Message));
        add({"email", "mail"}, action(ActionType::Mail));
        add({"browse", "url"}, action(ActionType::Url));
        add({"visit"}, action(ActionType::Visit));
        add({"google"}, action(ActionType::Google));

        add({"capturedmail"}, [](T t, C) { return t.uid.startsWith("mail-"); });  // Mail to Precrime tasks, which get uid "mail-…".
        add({"anylocation"}, [](T t, C c) { return !locationNamesOf(t, c).isEmpty(); });
        add({"nolocation"}, [](T t, C c) { return locationNamesOf(t, c).isEmpty(); });
        add({"alarms"}, [](T t, C c) { return c.store && !c.store->alarmsFor(t.id).isEmpty(); });
        add({"picture", "pict"}, [](T t, C c) { return hasAttachment(t, c, Attachment::Image); });
        add({"audio"}, [](T t, C c) { return hasAttachment(t, c, Attachment::Audio); });
        add({"note"}, [](T t, C) { return !t.notes.trimmed().isEmpty(); });

        add({"prio"}, [](T t, C) { return t.priority != Priority::None; });
        add({"noprio"}, [](T t, C) { return t.priority == Priority::None; });
        add({"hiprio"}, [](T t, C) { return t.priority == Priority::High; });
        add({"medprio"}, [](T t, C) { return t.priority == Priority::Medium; });
        add({"lowprio"}, [](T t, C) { return t.priority == Priority::Low; });

        add({"starred"}, [](T t, C) { return t.starred; });
        add({"repeat"}, [](T t, C) { return t.recurrence.isSet(); });
        add({"duration"}, [](T t, C) { return t.duration > 0; });
        add({"inprogress"}, [](T t, C c) {
            if (Schedule::isInProgress(t, c.now)) return true;
            const QDate today = c.now.date();
            return !t.completed && t.startDate.isValid() && !Schedule::isScheduled(t, c.now) &&
                   t.dueDate.isValid() && t.dueDate >= today;
        });
        add({"scheduled"}, [](T t, C c) { return !t.completed && Schedule::isScheduled(t, c.now); });
        add({"paused"}, [](T t, C c) {
            if (c.isPaused) return c.isPaused(t);
            if (!c.store) return false;
            for (qint64 id : t.tagIds) {
                auto it = c.store->tags().constFind(id);
                if (it != c.store->tags().constEnd() && it->paused && !it->deleted) return true;
            }
            return false;
        });

        add({"anytag"}, [](T t, C c) { return !tagNamesOf(t, c).isEmpty(); });
        add({"notag"}, [](T t, C c) { return tagNamesOf(t, c).isEmpty(); });

        add({"due"}, [](T t, C) { return t.dueDate.isValid(); });
        add({"nodue"}, [](T t, C) { return !t.dueDate.isValid(); });
        add({"start"}, [](T t, C) { return t.startDate.isValid(); });
        add({"nostart"}, [](T t, C) { return !t.startDate.isValid(); });

        add({"today", "duetoday"}, [day](T t, C c) {
            return Schedule::isDueToday(t, c.now) || dueWindowIntersects(t, day(c), day(c));
        });
        add({"tomorrow", "duetomorrow"}, [day](T t, C c) {
            const QDate d = day(c).addDays(1);
            return dueWindowIntersects(t, d, d);
        });
        add({"duethisweek"}, [week](T t, C c) {
            QDate a, b;
            week(c, a, b);
            return dueWindowIntersects(t, a, b);
        });
        add({"starttoday"}, [day](T t, C c) { return startWithin(t, day(c), day(c)); });
        add({"starttomorrow"}, [day](T t, C c) { return startWithin(t, day(c).addDays(1), day(c).addDays(1)); });
        add({"startthisweek"}, [week](T t, C c) {
            QDate a, b;
            week(c, a, b);
            return startWithin(t, a, b);
        });
        add({"anytoday"}, [day](T t, C c) {
            return Schedule::isDueToday(t, c.now) || dueWindowIntersects(t, day(c), day(c)) ||
                   startWithin(t, day(c), day(c));
        });
        add({"anytomorrow"}, [day](T t, C c) {
            const QDate d = day(c).addDays(1);
            return dueWindowIntersects(t, d, d) || startWithin(t, d, d);
        });
        add({"anythisweek", "thisweek"}, [week](T t, C c) {
            QDate a, b;
            week(c, a, b);
            return dueWindowIntersects(t, a, b) || startWithin(t, a, b);
        });
        add({"overdue", "tasksoverdue"}, [](T t, C c) { return Schedule::isOverdue(t, c.now, c.settings); });
        add({"donetoday"}, [day](T t, C c) { return doneWithin(t, day(c), day(c)); });
        add({"doneyesterday"}, [day](T t, C c) { return doneWithin(t, day(c).addDays(-1), day(c).addDays(-1)); });
        add({"donethisweek"}, [week](T t, C c) {
            QDate a, b;
            week(c, a, b);
            return doneWithin(t, a, b);
        });
        return m;
    }();
    return map;
}

LeafFactory typeFactory(QStringList *errors) {
    return [errors](const Tok &tok, bool negated) -> NodePtr {
        const QString name = tok.s.trimmed().toLower();
        auto it = typePresets().constFind(name);
        if (it == typePresets().constEnd()) {
            *errors << QStringLiteral("unknown type '%1'").arg(tok.s);
            return nullptr;
        }
        NodePtr n = leaf(it.value());
        return negated ? makeNot(n) : n;
    };
}

// -------------------------------------------------- tags/lists/groups/etc.

enum class NameKind { Tags, TagGroups, Lists, Groups, Locations };

struct NameSet {
    QStringList names;  // Folded.
    bool hasAny = false;
};

NameSet namesFor(NameKind kind, const Task &t, const QueryContext &c) {
    NameSet r;
    if (!c.store) return r;
    switch (kind) {
    case NameKind::Tags:
        for (const QString &n : tagNamesOf(t, c)) r.names << fold(n);
        r.hasAny = !r.names.isEmpty();
        break;
    case NameKind::TagGroups:
        for (qint64 id : t.tagIds) {
            auto it = c.store->tags().constFind(id);
            if (it == c.store->tags().constEnd() || it->deleted) continue;
            r.hasAny = true;
            auto g = c.store->tagGroups().constFind(it->groupId);
            if (g != c.store->tagGroups().constEnd() && !g->deleted) r.names << fold(g->name);
        }
        break;
    case NameKind::Lists:
    case NameKind::Groups: {
        qint64 listId = t.listId;
        if (!listId && t.parentId) listId = c.store->task(t.parentId).listId;
        auto l = c.store->lists().constFind(listId);
        if (l == c.store->lists().constEnd()) break;
        if (kind == NameKind::Lists) {
            r.names << fold(l->name);
        } else {
            auto g = c.store->groups().constFind(l->groupId);
            if (g != c.store->groups().constEnd() && !g->deleted) r.names << fold(g->name);
        }
        r.hasAny = !r.names.isEmpty();
        break;
    }
    case NameKind::Locations:
        for (const QString &n : locationNamesOf(t, c)) r.names << fold(n);
        r.hasAny = !r.names.isEmpty();
        break;
    }
    return r;
}

LeafFactory nameFactory(NameKind kind) {
    const bool presenceForNegation = kind == NameKind::Tags || kind == NameKind::TagGroups || kind == NameKind::Locations;
    return [kind, presenceForNegation](const Tok &tok, bool negated) -> NodePtr {
        const QString raw = tok.s.trimmed();
        if (raw.isEmpty()) return nullptr;
        if (tok.k == Tok::Word && raw == QLatin1String("~")) {
            NodePtr n = leaf([kind](const Task &t, const QueryContext &c) { return !namesFor(kind, t, c).hasAny; });
            return negated ? makeNot(n) : n;
        }
        if (tok.k == Tok::Word && raw == QLatin1String("?")) {
            NodePtr n = leaf([kind](const Task &t, const QueryContext &c) { return namesFor(kind, t, c).hasAny; });
            return negated ? makeNot(n) : n;
        }
        const QString pattern = fold(raw);
        return leaf([kind, pattern, negated, presenceForNegation](const Task &t, const QueryContext &c) {
            const NameSet s = namesFor(kind, t, c);
            bool any = false;
            for (const QString &n : s.names)
                if (nameMatches(pattern, n)) { any = true; break; }
            if (!negated) return any;
            return !any && (!presenceForNegation || s.hasAny);
        });
    };
}

// ---------------------------------------------------- modified:/created:

enum class TimeField { Modified, Created, Completed };

LeafFactory timeFactory(TimeField field, QStringList *errors) {
    static const QRegularExpression re(QStringLiteral(R"(^\s*([<>]?)\s*(\d+(?:\.\d+)?|\.\d+)\s*([a-zA-Z]*)\s*$)"));
    return [field, errors](const Tok &tok, bool negated) -> NodePtr {
        const auto m = re.match(tok.s);
        double unitMinutes = 0;
        if (m.hasMatch()) {
            const QString u = m.captured(3).toLower();
            if (u.isEmpty() || u == "d" || u == "day" || u == "days") unitMinutes = 1440;
            else if (u == "mins" || u == "min" || u == "minute" || u == "minutes") unitMinutes = 1;
            else if (u == "h" || u == "hr" || u == "hrs" || u == "hour" || u == "hours") unitMinutes = 60;
            else if (u == "w" || u == "wk" || u == "week" || u == "weeks") unitMinutes = 10080;
            else if (u == "m" || u == "month" || u == "months") unitMinutes = 1440 * 30.5;
            else if (u == "y" || u == "yr" || u == "year" || u == "years") unitMinutes = 1440 * 365;
        }
        if (unitMinutes == 0) {
            *errors << QStringLiteral("bad time '%1'").arg(tok.s);
            return nullptr;
        }
        const bool older = m.captured(1) == QLatin1String(">");
        const qint64 ms = qint64(m.captured(2).toDouble() * unitMinutes * 60000.0);
        NodePtr n = leaf([field, older, ms](const Task &t, const QueryContext &c) {
            QDateTime v;
            switch (field) {
            case TimeField::Modified: v = t.modified; break;
            case TimeField::Created: v = t.created; break;
            case TimeField::Completed: v = t.completed ? t.completedAt : QDateTime(); break;
            }
            if (!v.isValid()) return false;
            const QDateTime cutoff = c.now.addMSecs(-ms);
            return older ? v < cutoff : v >= cutoff;
        });
        return negated ? makeNot(n) : n;
    };
}

// ----------------------------------------------------------------- parse

struct Segment {
    enum Kind { Text, Type, Tags, TagGroups, Locations, Lists, Groups, Modified, Created, Completed, DateFilter, Group } kind = Text;
    bool negated = false;  // For "!modified:" and friends.
    bool orMarked = false;
    QString value;
    NodePtr node;          // A Group's already parsed contents.
};

NodePtr buildSegment(const Segment &seg, QStringList *errors) {
    TokOptions o;
    LeafFactory f;
    switch (seg.kind) {
    case Segment::Text:
        o.dashNegates = true;
        f = [errors](const Tok &t, bool neg) { return textLeaf(t, neg, errors); };
        break;
    case Segment::Type:
        o.commaIsOr = true;
        f = typeFactory(errors);
        break;
    case Segment::Tags: o.commaIsOr = o.mergeWords = true; f = nameFactory(NameKind::Tags); break;
    case Segment::TagGroups: o.commaIsOr = o.mergeWords = true; f = nameFactory(NameKind::TagGroups); break;
    case Segment::Locations: o.commaIsOr = o.mergeWords = true; f = nameFactory(NameKind::Locations); break;
    case Segment::Lists: o.commaIsOr = o.mergeWords = true; f = nameFactory(NameKind::Lists); break;
    case Segment::Groups: o.commaIsOr = o.mergeWords = true; f = nameFactory(NameKind::Groups); break;
    case Segment::Modified: o.commaIsOr = o.mergeWords = true; f = timeFactory(TimeField::Modified, errors); break;
    case Segment::Created: o.commaIsOr = o.mergeWords = true; f = timeFactory(TimeField::Created, errors); break;
    case Segment::Completed: o.commaIsOr = o.mergeWords = true; f = timeFactory(TimeField::Completed, errors); break;
    case Segment::Group:
        return seg.negated ? makeNot(seg.node) : seg.node;
    case Segment::DateFilter:
        return leaf([](const Task &t, const QueryContext &c) {
            if (!c.rangeFrom.isValid() && !c.rangeTo.isValid()) return true;
            if (!t.dueDate.isValid()) return false;
            if (c.rangeFrom.isValid() && t.dueDate < c.rangeFrom) return false;
            if (c.rangeTo.isValid() && t.dueDate > c.rangeTo) return false;
            return true;
        });
    }
    ExprParser parser(tokenize(seg.value, o), f, errors);
    NodePtr n = parser.parse();
    return seg.negated ? makeNot(n) : n;
}

// Splits off a trailing standalone OR; returns true if there was one.
bool stripTrailingOr(QString &s) {
    static const QRegularExpression re(QStringLiteral(R"((?:^|\s)or\s*$)"), QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(s);
    if (!m.hasMatch()) return false;
    s.truncate(m.capturedStart());
    return true;
}

const QRegularExpression &keywordPattern() {
    static const QRegularExpression keyword(
        QStringLiteral(R"((?<!\S)(?:type:|tags:|locations:|lists:|groups:|taggroups:|!?(?:modified:|created:|completed:)|!?\{group:\d+\}|\{datefilter\}))"),
        QRegularExpression::CaseInsensitiveOption);
    return keyword;
}

// Parenthesised groups that open with a keyword filter, such as
// "(tags: a OR tags: b)", combine whole filters, which the flat keyword
// segments cannot. They are lifted out as `{group:N}` placeholders (keeping a
// leading "!") with their contents in `inners`. Parentheses inside a filter's
// own value, "type: (hiprio OR starred)", are left alone.
QString liftGroups(const QString &text, QStringList &inners) {
    static const QRegularExpression opensWithKeyword(
        QStringLiteral(R"(^\s*!?(?:type:|tags:|locations:|lists:|groups:|taggroups:|modified:|created:|completed:|\{datefilter\}|\{group:))"),
        QRegularExpression::CaseInsensitiveOption);
    QString out;
    bool quoted = false;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c == '"') quoted = !quoted;
        if (c != '(' || quoted) {
            out += c;
            continue;
        }
        int depth = 0, close = -1;
        bool q = false;
        for (int j = i; j < text.size(); ++j) {
            const QChar d = text.at(j);
            if (d == '"') q = !q;
            else if (!q && d == '(') ++depth;
            else if (!q && d == ')' && --depth == 0) { close = j; break; }
        }
        if (close < 0) {
            out += c;
            continue;
        }
        const QString inner = text.mid(i + 1, close - i - 1);
        if (!opensWithKeyword.match(inner).hasMatch()) {
            out += c;
            continue;
        }
        const bool negated = out.endsWith('!');
        if (negated) out.chop(1);
        out += QStringLiteral(" %1{group:%2} ").arg(negated ? "!" : "").arg(inners.size());
        inners << inner;
        i = close;
    }
    return out;
}

}  // namespace

Query Query::parse(const QString &input, QString *error) {
    const QRegularExpression &keyword = keywordPattern();

    // A negated value (!donetoday) does not ask for completed tasks.
    static const QRegularExpression doneTypes(QStringLiteral(R"((?<![!\w])done(?:today|yesterday|thisweek)\b)"),
                                              QRegularExpression::CaseInsensitiveOption);

    QStringList errors;
    std::vector<Segment> segs;
    QStringList inners;
    const QString text = liftGroups(input, inners);
    std::vector<Query> subQueries;
    for (const QString &inner : inners) {
        QString subError;
        subQueries.push_back(Query::parse(inner, &subError));
        if (!subError.isEmpty()) errors << subError;
    }

    struct Hit { int start, end; QString word; };
    std::vector<Hit> hits;
    auto it = keyword.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        hits.push_back({int(m.capturedStart()), int(m.capturedEnd()), m.captured().toLower()});
    }

    bool namesCompleted = false, namesScheduled = false, namesPaused = false, replacesScope = false;
    bool nextOr = false;
    {
        QString pre = text.left(hits.empty() ? text.size() : hits.front().start);
        if (!hits.empty()) nextOr = stripTrailingOr(pre);
        if (!pre.trimmed().isEmpty()) {
            Segment s;
            s.value = pre;
            segs.push_back(s);
        }
    }
    for (size_t i = 0; i < hits.size(); ++i) {
        const int vEnd = i + 1 < hits.size() ? hits[i + 1].start : text.size();
        QString value = text.mid(hits[i].end, vEnd - hits[i].end);
        Segment s;
        s.orMarked = nextOr;
        nextOr = i + 1 < hits.size() ? stripTrailingOr(value) : false;
        s.value = value;
        QString w = hits[i].word;
        if (w.startsWith('!')) { s.negated = true; w.remove(0, 1); }
        if (w.startsWith("{group:")) {
            s.kind = Segment::Group;
            const Query &sub = subQueries.at(w.mid(7, w.size() - 8).toInt());
            s.node = std::const_pointer_cast<Node>(sub.m_root);
            if (!s.node) continue;
            // Anything after the group up to the next keyword is plain text.
            if (!value.trimmed().isEmpty()) {
                Segment extra;
                extra.value = value;
                s.value.clear();
                segs.push_back(s);
                segs.push_back(extra);
            } else {
                segs.push_back(s);
            }
            namesCompleted = namesCompleted || sub.m_namesCompleted;
            namesScheduled = namesScheduled || sub.m_namesScheduled;
            namesPaused = namesPaused || sub.m_namesPaused;
            replacesScope = replacesScope || sub.m_replacesScope;
            continue;
        }
        if (w == "type:") s.kind = Segment::Type;
        else if (w == "tags:") s.kind = Segment::Tags;
        else if (w == "taggroups:") s.kind = Segment::TagGroups;
        else if (w == "locations:") s.kind = Segment::Locations;
        else if (w == "lists:") s.kind = Segment::Lists;
        else if (w == "groups:") s.kind = Segment::Groups;
        else if (w == "modified:") s.kind = Segment::Modified;
        else if (w == "created:") s.kind = Segment::Created;
        else if (w == "completed:") s.kind = Segment::Completed;
        else {
            s.kind = Segment::DateFilter;
            // Anything after {datefilter} up to the next keyword is plain text.
            if (!value.trimmed().isEmpty()) {
                Segment extra;
                extra.value = value;
                s.value.clear();
                segs.push_back(s);
                segs.push_back(extra);
                continue;
            }
        }
        segs.push_back(s);
    }

    std::vector<NodePtr> group, alts;
    for (const Segment &s : segs) {
        NodePtr n = buildSegment(s, &errors);
        if (!n) continue;
        (s.orMarked ? alts : group).push_back(n);
    }
    NodePtr root;
    if (alts.empty()) {
        root = combine(Node::And, group);
    } else {
        std::vector<NodePtr> branches;
        if (NodePtr g = combine(Node::And, group)) branches.push_back(g);
        for (const auto &a : alts) branches.push_back(a);
        root = combine(Node::Or, branches);
    }

    if (error) *error = errors.join(QStringLiteral("; "));
    Query q;
    q.m_root = root;
    q.m_namesCompleted = namesCompleted;
    q.m_namesScheduled = namesScheduled;
    q.m_namesPaused = namesPaused;
    q.m_replacesScope = replacesScope;
    for (const Segment &s : segs) {
        const bool done = s.kind == Segment::Completed && !s.negated;
        const bool doneType = s.kind == Segment::Type && !s.negated &&
                              s.value.contains(doneTypes);
        if (done || doneType) q.m_namesCompleted = q.m_replacesScope = true;
        if (s.kind == Segment::Type && !s.negated) {
            static const QRegularExpression scheduled(QStringLiteral(R"((?<![!\w])scheduled\b)"), QRegularExpression::CaseInsensitiveOption);
            static const QRegularExpression paused(QStringLiteral(R"((?<![!\w])paused\b)"), QRegularExpression::CaseInsensitiveOption);
            if (s.value.contains(scheduled)) q.m_namesScheduled = true;
            if (s.value.contains(paused)) q.m_namesPaused = true;
        }
        if (s.kind == Segment::Lists || s.kind == Segment::Groups) q.m_replacesScope = true;
    }
    return q;
}

bool Query::isEmpty() const { return !m_root; }

bool Query::matches(const Task &task, const QueryContext &context) const {
    return !m_root || m_root->eval(task, context);
}
