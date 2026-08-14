#include "core/Scheduler.h"

#include "core/JobManager.h"
#include "core/Models.h"
#include "core/ProjectStore.h"

#include <QJsonArray>
#include <QTimeZone>

namespace CyberSnapper {

namespace {

QTimeZone scheduleZone(const QJsonObject &recurrence) {
  const QByteArray requested = recurrence.value("timeZone").toString().toUtf8();
  return requested.isEmpty() ? QTimeZone::systemTimeZone() : QTimeZone(requested);
}

QTime scheduleTime(const QJsonObject &recurrence) {
  return QTime::fromString(recurrence.value("time").toString("09:00"), "HH:mm");
}

QDateTime validLocalDateTime(QDate date, QTime time, const QTimeZone &zone) {
  QDateTime candidate(date, time, zone);
  for (int i = 0; i < 180 && !candidate.isValid(); ++i) candidate = QDateTime(date, time.addSecs(60 * (i + 1)), zone);
  return candidate;
}

} // namespace

Scheduler::Scheduler(JobManager *jobs, StoreProvider stores, Submitter submitter, QObject *parent)
    : QObject(parent), m_jobs(jobs), m_stores(std::move(stores)),
      m_submitter(std::move(submitter)) {
  m_timer.setInterval(30000);
  connect(&m_timer, &QTimer::timeout, this, &Scheduler::checkNow);
  connect(m_jobs, &JobManager::eventPublished, this, &Scheduler::handleJobEvent);
}

void Scheduler::start() {
  m_timer.start();
  QTimer::singleShot(0, this, &Scheduler::checkNow);
}

void Scheduler::stop() { m_timer.stop(); }

QDateTime Scheduler::nextOccurrence(const QJsonObject &recurrence, const QDateTime &afterUtc, QString *error) {
  const QString type = recurrence.value("type").toString();
  if (type == "once") {
    const QDateTime at = QDateTime::fromString(recurrence.value("at").toString(), Qt::ISODate);
    if (at.isValid() && at.toUTC() > afterUtc.toUTC()) return at.toUTC();
    if (error) *error = "The one-time run must be a valid future date and time";
    return {};
  }
  if (type == "interval") {
    const int minutes = qBound(15, recurrence.value("minutes").toInt(60), 525600);
    return afterUtc.toUTC().addSecs(minutes * 60);
  }

  const QTimeZone zone = scheduleZone(recurrence);
  if (!zone.isValid()) { if (error) *error = "The schedule time zone is invalid"; return {}; }
  const QDateTime localAfter = afterUtc.toTimeZone(zone);
  const QTime time = scheduleTime(recurrence);
  if (!time.isValid()) { if (error) *error = "Use a valid 24-hour time such as 09:00"; return {}; }
  if (type == "daily") {
    QDate date = localAfter.date();
    QDateTime candidate = validLocalDateTime(date, time, zone);
    if (candidate <= localAfter) candidate = validLocalDateTime(date.addDays(1), time, zone);
    return candidate.toUTC();
  }
  if (type == "weekly") {
    QSet<int> days;
    for (const auto &entry : recurrence.value("daysOfWeek").toArray()) {
      const int day = entry.toInt();
      if (day >= 1 && day <= 7) days.insert(day);
    }
    if (days.isEmpty()) { if (error) *error = "A weekly schedule needs at least one weekday"; return {}; }
    for (int add = 0; add <= 7; ++add) {
      const QDate date = localAfter.date().addDays(add);
      if (!days.contains(date.dayOfWeek())) continue;
      const QDateTime candidate = validLocalDateTime(date, time, zone);
      if (candidate > localAfter) return candidate.toUTC();
    }
  }
  if (type == "monthly") {
    const QJsonValue dayValue = recurrence.value("day");
    if (dayValue.isString() && dayValue.toString() != "last") {
      if (error) *error = "Monthly day must be 1 through 31 or last";
      return {};
    }
    if (!dayValue.isString() && (dayValue.toInt() < 1 || dayValue.toInt() > 31)) {
      if (error) *error = "Monthly day must be 1 through 31 or last";
      return {};
    }
    for (int addMonth = 0; addMonth <= 12; ++addMonth) {
      const QDate month = QDate(localAfter.date().year(), localAfter.date().month(), 1).addMonths(addMonth);
      const int day = dayValue.toString() == "last"
          ? month.daysInMonth()
          : qBound(1, dayValue.toInt(1), month.daysInMonth());
      const QDateTime candidate = validLocalDateTime(QDate(month.year(), month.month(), day), time, zone);
      if (candidate > localAfter) return candidate.toUTC();
    }
  }
  if (error) *error = "Unsupported or invalid recurrence";
  return {};
}

void Scheduler::checkNow() {
  const QDateTime now = QDateTime::currentDateTimeUtc();
  for (ProjectStore *store : m_stores()) {
    if (!store || !store->isOpen()) continue;
    for (const auto &value : store->schedules(true)) {
      const QJsonObject schedule = value.toObject();
      const QString scheduleId = schedule.value("id").toString();
      QDateTime due = QDateTime::fromString(schedule.value("nextRun").toString(), Qt::ISODate);
      if (!due.isValid()) {
        QString recurrenceError;
        due = nextOccurrence(schedule.value("recurrence").toObject(), now.addSecs(-1), &recurrenceError);
        if (!due.isValid()) {
          emit scheduleEvent(store->projectId(), {{"type", "schedule_error"}, {"scheduleId", scheduleId},
                                                  {"message", recurrenceError}});
          continue;
        }
        store->updateScheduleRun(scheduleId, schedule.value("lastRun").toString(), due.toString(Qt::ISODateWithMs), "ready");
      }
      if (due > now) continue;

      QString recurrenceError;
      const QDateTime next = nextOccurrence(schedule.value("recurrence").toObject(), now, &recurrenceError);
      if (m_inFlightSchedules.contains(scheduleId)) {
        store->updateScheduleRun(scheduleId, schedule.value("lastRun").toString(),
                                 next.toString(Qt::ISODateWithMs), "coalesced");
        emit scheduleEvent(store->projectId(), {{"type", "schedule_coalesced"}, {"scheduleId", scheduleId}});
        continue;
      }

      JobRequest request;
      request.id = newId();
      request.projectId = store->projectId();
      request.projectRoot = store->root();
      request.profileId = schedule.value("profileId").toString("default");
      request.profile = store->profile(request.profileId);
      request.source = "schedule:" + scheduleId;
      for (const auto &url : schedule.value("urls").toArray()) request.urls.append(url.toString());
      QString submitError;
      const QString jobId = m_submitter
          ? m_submitter(store, request, &submitError)
          : m_jobs->submit(store, request, &submitError);
      const QString status = jobId.isEmpty() ? "failed" : "queued";
      if (schedule.value("recurrence").toObject().value("type").toString() == "once") {
        QJsonObject completedOnce = schedule;
        completedOnce.insert("enabled", false);
        completedOnce.insert("lastRun", now.toString(Qt::ISODateWithMs));
        completedOnce.insert("nextRun", QString());
        completedOnce.insert("lastStatus", status);
        store->upsertSchedule(completedOnce);
      } else {
        store->updateScheduleRun(scheduleId, now.toString(Qt::ISODateWithMs),
                                 next.toString(Qt::ISODateWithMs), status);
      }
      if (!jobId.isEmpty()) {
        m_inFlightSchedules.insert(scheduleId);
        m_jobToSchedule.insert(jobId, scheduleId);
      }
      emit scheduleEvent(store->projectId(), {{"type", jobId.isEmpty() ? "schedule_error" : "schedule_started"},
                                              {"scheduleId", scheduleId}, {"jobId", jobId}, {"message", submitError}});
    }
  }
}

void Scheduler::handleJobEvent(const QString &, const QJsonObject &event) {
  const QString type = event.value("type").toString();
  if (!QStringList{"job_succeeded", "job_partial", "job_failed", "job_cancelled"}.contains(type)) return;
  const QString scheduleId = m_jobToSchedule.take(event.value("jobId").toString());
  if (!scheduleId.isEmpty()) m_inFlightSchedules.remove(scheduleId);
}

} // namespace CyberSnapper
