#pragma once

// The search and Smart List language: free text with implicit AND, OR,
// -word / !word, word*, *word, "phrases", parentheses, and keyword filters
// (type:, tags:, taggroups:, locations:, lists:, groups:, modified:,
// created:, completed:, {datefilter}). One parsed Query is evaluated against
// many tasks.

#include <QDate>
#include <QDateTime>
#include <QString>
#include <functional>
#include <memory>

#include "model.h"
#include "schedule.h"

class Store;

struct QueryContext {
    const Store *store = nullptr;
    QDateTime now;
    ScheduleSettings settings;
    bool soundex = false;                          // Fuzzy "sounds like" word matching.
    QDate rangeFrom;                               // What {datefilter} stands for; invalid when unset.
    QDate rangeTo;
    std::function<bool(const Task &)> isPaused;    // May be empty.
};

class Query {
public:
    struct Node;

    Query();
    ~Query();
    Query(const Query &);
    Query &operator=(const Query &);

    // Never fails: unknown keywords and stray operators degrade to plain text,
    // so a half-typed search still filters sensibly. `error` describes what
    // was ignored, for an inline hint.
    static Query parse(const QString &text, QString *error = nullptr);

    bool isEmpty() const;
    // Whether the query asks for completed tasks (completed:, type: donetoday
    // and friends); views then show them whatever the completed-visibility
    // setting says.
    bool namesCompleted() const { return m_namesCompleted; }
    // Likewise for type: scheduled and type: paused, which would otherwise
    // find nothing, as scheduled and paused tasks are hidden by default.
    bool namesScheduled() const { return m_namesScheduled; }
    bool namesPaused() const { return m_namesPaused; }
    // Whether the query names lists, groups or completed tasks. Such a search
    // looks across every list, not just the one on screen.
    bool replacesScope() const { return m_replacesScope; }
    bool matches(const Task &task, const QueryContext &context) const;

private:
    std::shared_ptr<const Node> m_root;
    bool m_namesCompleted = false;
    bool m_namesScheduled = false;
    bool m_namesPaused = false;
    bool m_replacesScope = false;
};
