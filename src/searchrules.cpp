#include "searchrules.h"

#include <QHash>
#include <memory>
#include <vector>

namespace SearchRules {

namespace {

struct Value {
    const char *token;
    const char *label;
};

struct Property {
    const char *key;
    const char *title;
    const char *pos;
    const char *neg;
    std::vector<Value> values;
    const char *negToken;  // Written instead of "!" + token when set.
};

const std::vector<Property> &table() {
    static const std::vector<Property> t = {
        {"task", "Task", "is", "is not",
         {{"task", "Task"}, {"subtasks", "Sub-task"}, {"project", "Project"}, {"checklist", "Checklist"},
          {"emptyproj", "Empty Project"}, {"emptychck", "Empty Checklist"}, {"inprogress", "In Progress"},
          {"paused", "Paused"}, {"scheduled", "Scheduled"}}, nullptr},
        {"note", "Note", "has", "doesn't have", {{"note", "a note"}}, nullptr},
        {"repeat", "Repeating", "is", "is not", {{"repeat", "Repeating"}}, nullptr},
        {"due", "Due Date", "is", "is not",
         {{"due", "Any Date"}, {"nodue", "No Date"}, {"overdue", "Overdue"}, {"duetoday", "Today"},
          {"duetomorrow", "Tomorrow"}, {"duethisweek", "This Week"}}, nullptr},
        {"start", "Start Date", "is", "is not",
         {{"start", "Any Date"}, {"nostart", "No Date"}, {"starttoday", "Today"}, {"starttomorrow", "Tomorrow"},
          {"startthisweek", "This Week"}}, nullptr},
        {"any", "Any Date", "is", "is not",
         {{"anytoday", "Today"}, {"anytomorrow", "Tomorrow"}, {"anythisweek", "This Week"}}, nullptr},
        {"duration", "Duration", "has", "doesn't have", {{"duration", "a duration"}}, nullptr},
        {"completion", "Completion", "completed", "not completed",
         {{"donetoday", "Today"}, {"doneyesterday", "Yesterday"}, {"donethisweek", "This Week"}}, nullptr},
        {"alarm", "Alarm", "has", "doesn't have", {{"alarms", "an alarm"}}, nullptr},
        {"starred", "Starred", "is", "is not", {{"starred", "Starred"}}, nullptr},
        {"priority", "Priority", "is", "is not",
         {{"prio", "Any"}, {"noprio", "No"}, {"lowprio", "Low"}, {"medprio", "Medium"}, {"hiprio", "High"}}, nullptr},
        {"tag", "Tag", "is set", "is not set", {{"anytag", "set"}}, "notag"},
        {"location", "Location", "is set", "is not set", {{"anylocation", "set"}}, "nolocation"},
        {"picture", "Picture", "attached", "not attached", {{"picture", "attached"}}, nullptr},
        {"audio", "Audio", "attached", "not attached", {{"audio", "attached"}}, nullptr},
        {"action", "Action", "is", "is not",
         {{"action", "Any Action"}, {"call", "Call"}, {"sms", "Message"}, {"url", "URL"}, {"email", "Email"},
          {"visit", "Visit"}, {"google", "Google"}}, nullptr},
    };
    return t;
}

// Spellings the query language accepts for the same preset.
const QHash<QString, QString> &aliases() {
    static const QHash<QString, QString> a = {
        {"today", "duetoday"}, {"tomorrow", "duetomorrow"}, {"thisweek", "anythisweek"}, {"tasksoverdue", "overdue"},
        {"proj", "project"}, {"chck", "checklist"}, {"message", "sms"}, {"mail", "email"}, {"browse", "url"},
        {"pict", "picture"},
    };
    return a;
}

struct Hit {
    const Property *prop;
    bool neg;
    const char *token;
};

// Every preset token, including the notag / nolocation negatives.
const QHash<QString, Hit> &index() {
    static const QHash<QString, Hit> idx = [] {
        QHash<QString, Hit> m;
        for (const Property &p : table()) {
            for (const Value &v : p.values) m.insert(QString::fromLatin1(v.token), {&p, false, v.token});
            if (p.negToken) m.insert(QString::fromLatin1(p.negToken), {&p, true, p.values.front().token});
        }
        return m;
    }();
    return idx;
}

const Property *propertyFor(const QString &key) {
    for (const Property &p : table())
        if (key == QLatin1String(p.key)) return &p;
    return nullptr;
}

// ---- parsing

struct Node {
    enum Kind { Leaf, And, Or } kind = Leaf;
    QVariantMap clause;
    std::vector<std::unique_ptr<Node>> kids;
};

using NodePtr = std::unique_ptr<Node>;

class Parser {
public:
    explicit Parser(const QString &text) { lex(text); }

    NodePtr run() {
        if (!m_ok || m_toks.empty()) return nullptr;
        NodePtr n = parseOr(0);
        if (!m_ok || m_p != m_toks.size()) return nullptr;
        return n;
    }

private:
    struct Tok {
        enum K { LP, RP, Comma, Not, Or, And, Word } k;
        QString s;
    };

    void lex(const QString &text) {
        int i = 0;
        const int n = text.size();
        while (i < n) {
            const QChar c = text.at(i);
            if (c.isSpace()) { ++i; continue; }
            if (c == '(') { m_toks.push_back({Tok::LP, {}}); ++i; continue; }
            if (c == ')') { m_toks.push_back({Tok::RP, {}}); ++i; continue; }
            if (c == ',') { m_toks.push_back({Tok::Comma, {}}); ++i; continue; }
            if (c == '!') { m_toks.push_back({Tok::Not, {}}); ++i; continue; }
            if (c == '"') { m_ok = false; return; }
            int j = i;
            while (j < n && !text.at(j).isSpace() && !QStringLiteral("(),!\"").contains(text.at(j))) ++j;
            const QString w = text.mid(i, j - i).toLower();
            if (w == QLatin1String("or")) m_toks.push_back({Tok::Or, {}});
            else if (w == QLatin1String("and")) m_toks.push_back({Tok::And, {}});
            else m_toks.push_back({Tok::Word, w});
            i = j;
        }
    }

    bool at(Tok::K k) const { return m_p < m_toks.size() && m_toks[m_p].k == k; }

    NodePtr parseOr(int depth) {
        std::vector<NodePtr> kids;
        kids.push_back(parseAnd(depth));
        while (m_ok && (at(Tok::Or) || at(Tok::Comma))) {
            ++m_p;
            kids.push_back(parseAnd(depth));
        }
        return join(Node::Or, std::move(kids));
    }

    NodePtr parseAnd(int depth) {
        std::vector<NodePtr> kids;
        while (m_ok && m_p < m_toks.size()) {
            const Tok::K k = m_toks[m_p].k;
            if (k == Tok::RP || k == Tok::Or || k == Tok::Comma) break;
            if (k == Tok::And) { ++m_p; continue; }
            kids.push_back(parseUnary(depth));
        }
        if (kids.empty()) m_ok = false;
        return join(Node::And, std::move(kids));
    }

    NodePtr parseUnary(int depth) {
        const Tok tok = m_toks[m_p++];
        if (tok.k == Tok::LP) {
            if (depth >= 64) { m_ok = false; return nullptr; }
            NodePtr n = parseOr(depth + 1);
            if (!m_ok || !at(Tok::RP)) { m_ok = false; return nullptr; }
            ++m_p;
            return n;
        }
        bool neg = false;
        Tok word = tok;
        if (tok.k == Tok::Not) {
            // A negated parenthesis, a doubled "!" or a dangling "!" have no row form.
            if (!at(Tok::Word)) { m_ok = false; return nullptr; }
            neg = true;
            word = m_toks[m_p++];
        }
        if (word.k != Tok::Word) { m_ok = false; return nullptr; }
        const QString name = aliases().value(word.s, word.s);
        const auto it = index().constFind(name);
        if (it == index().constEnd()) { m_ok = false; return nullptr; }
        auto n = std::make_unique<Node>();
        n->clause = {{"prop", QString::fromLatin1(it->prop->key)}, {"neg", neg != it->neg},
                     {"value", QString::fromLatin1(it->token)}};
        return n;
    }

    // One kid stands for itself; same-kind children are merged ("(a AND b) AND c").
    NodePtr join(Node::Kind kind, std::vector<NodePtr> kids) {
        if (!m_ok) return nullptr;
        if (kids.size() == 1) return std::move(kids.front());
        auto n = std::make_unique<Node>();
        n->kind = kind;
        for (NodePtr &k : kids) {
            if (k->kind == kind) {
                for (NodePtr &g : k->kids) n->kids.push_back(std::move(g));
            } else {
                n->kids.push_back(std::move(k));
            }
        }
        return n;
    }

    std::vector<Tok> m_toks;
    size_t m_p = 0;
    bool m_ok = true;
};

QVariantMap toGroup(const Node &n) {
    QVariantList items;
    for (const NodePtr &k : n.kids) {
        // A child group has the other join, so it stays a nested group.
        if (k->kind == Node::Leaf) items << k->clause;
        else items << toGroup(*k);
    }
    return {{"match", n.kind == Node::Or ? "any" : "all"}, {"items", items}};
}

// ---- writing

QString clauseToken(const QVariantMap &c) {
    const Property *p = propertyFor(c.value("prop").toString());
    if (!p) return {};
    const QString want = c.value("value").toString();
    const char *token = p->values.front().token;
    for (const Value &v : p->values)
        if (want == QLatin1String(v.token)) token = v.token;
    const QString t = QString::fromLatin1(token);
    if (!c.value("neg").toBool()) return t;
    return p->negToken ? QString::fromLatin1(p->negToken) : QStringLiteral("!") + t;
}

// Returns the text of a group; *count is how many items went into it.
QString groupText(const QVariantMap &g, int *count) {
    const QString join = g.value("match").toString() == QLatin1String("any") ? QStringLiteral(" OR ")
                                                                           : QStringLiteral(" AND ");
    QStringList parts;
    const QVariantList items = g.value("items").toList();
    for (const QVariant &v : items) {
        const QVariantMap m = v.toMap();
        if (m.contains("items")) {
            int n = 0;
            const QString inner = groupText(m, &n);
            if (n == 0) continue;
            parts << (n > 1 ? QStringLiteral("(") + inner + QStringLiteral(")") : inner);
        } else {
            const QString t = clauseToken(m);
            if (!t.isEmpty()) parts << t;
        }
    }
    *count = int(parts.size());
    return parts.join(join);
}

}  // namespace

QVariantMap fromType(const QString &typeValue, bool *ok) {
    NodePtr n = Parser(typeValue).run();
    if (ok) *ok = bool(n);
    if (!n) return {};
    if (n->kind == Node::Leaf) return {{"match", "all"}, {"items", QVariantList{n->clause}}};
    return toGroup(*n);
}

QString toType(const QVariantMap &tree) {
    int n = 0;
    return groupText(tree, &n);
}

QVariantList properties() {
    QVariantList out;
    for (const Property &p : table()) {
        QVariantList values;
        for (const Value &v : p.values)
            values << QVariantMap{{"token", QString::fromLatin1(v.token)}, {"label", QString::fromLatin1(v.label)}};
        out << QVariantMap{{"key", QString::fromLatin1(p.key)},
                           {"title", QString::fromLatin1(p.title)},
                           {"ops", QStringList{QString::fromLatin1(p.pos), QString::fromLatin1(p.neg)}},
                           {"values", values},
                           {"single", p.values.size() == 1}};
    }
    return out;
}

}  // namespace SearchRules
