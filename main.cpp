#include <QCoreApplication>
#include <QDebug>
#include <QStringList>
#include <sqlite3.h>
#include <QDateTime>
#include <QUuid>

#include <CommHistory/Event>
#include <CommHistory/EventModel>
#include <CommHistory/Group>
#include <CommHistory/GroupModel>

#include "catcher.h"

// service_id
#define N900_CHAT         2
#define N900_SMS          3

#define ACCOUNT_PREFIX "/org/freedesktop/Telepathy/Account/"

#define FILTER "ring"
//#define FILTER "40gmail_2ecom0, skype, ring"

using namespace CommHistory;

class Filter {
public:
  Filter() {
    QStringList l = QString(FILTER).trimmed().split(",", QString::SkipEmptyParts);
    foreach (const QString& s, l) {
      f << s.trimmed();
    }
  }

  bool add(const QString& local) {
    if (f.isEmpty()) {
      return true;
    }

    foreach(const QString& s, f) {
      if (local.contains(s)) {
	return true;
      }
    }

    return false;
  }

private:
  QStringList f;
};

class GroupFinder {
public:
  GroupFinder() {
    m.setQueryMode(EventModel::SyncQuery);
  }

  bool init() {
    if (!m.getGroups()) {
      return false;
    }

    for (int x = 0; x < m.rowCount(); x++) {
      Group g = m.group(m.index(x, 1));
      map[g.localUid()][g.remoteUids().at(0)] = g.id();
    }

    return true;
  }

  int groupId(const QString& local, const QString& remote) {
    if (map.contains(local) && map[local].contains(remote)) {
      return map[local][remote];
    }

    return -1;
  }


  int newGroup(const QString& local, const QString& remote) {
    Group g;
    g.setChatType(Group::ChatTypeP2P);
    g.setLocalUid(local);
    g.setRemoteUids(QStringList() << remote);

    if (!m.addGroup(g)) {
      return -1;
    }

    map[local][remote] = g.id();

    return g.id();
  }

private:
  GroupModel m;
  QMap<QString, QMap<QString, int> > map;
};


bool readDb(const char *path, QList<Event>& events) {
#define ENSURE(x) { if (x != SQLITE_OK) { qCritical() << sqlite3_errmsg(db); return false;} }
#define QUERY "SELECT id, service_id, start_time, end_time, bytes_sent, bytes_received, local_uid, local_name, remote_uid, channel, free_text, group_uid, outgoing FROM Events WHERE service_id == 2 OR service_id == 3;"

  Filter filter;

  sqlite3 *db = 0;
  sqlite3_stmt *stmt = 0;

  ENSURE(sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, NULL));
  ENSURE(sqlite3_prepare_v2(db, QUERY, -1, &stmt, NULL));

  int ret = sqlite3_step(stmt);
  while (ret == SQLITE_ROW) {

    ////    int id = sqlite3_column_int(stmt, 0);
    int service_id = sqlite3_column_int(stmt, 1);

    QDateTime start_time = QDateTime::fromTime_t(sqlite3_column_int(stmt, 2));
    QDateTime end_time = QDateTime::fromTime_t(sqlite3_column_int(stmt, 3));

    ////    int bytes_sent = sqlite3_column_int(stmt, 4);
    int bytes_received = sqlite3_column_int(stmt, 5);
    QString local_uid = QString::fromUtf8((const char *)sqlite3_column_text(stmt, 6));
    ////    QString local_name = QString::fromUtf8((const char *)sqlite3_column_text(stmt, 7));
    QString remote_uid = QString::fromUtf8((const char *)sqlite3_column_text(stmt, 8));
    ////    QString channel = QString::fromUtf8((const char *)sqlite3_column_text(stmt, 9));
    QString free_text = QString::fromUtf8((const char *)sqlite3_column_text(stmt, 10));
    ////    QString group_uid = QString::fromUtf8((const char *)sqlite3_column_text(stmt, 11));
    bool outgoing = sqlite3_column_int(stmt, 12) == 1;

    if (filter.add(local_uid)) {
      Event e;
      e.setType(service_id == N900_SMS ? Event::SMSEvent : Event::IMEvent);
      e.setStartTime(start_time);
      e.setEndTime(end_time);
      e.setDirection(outgoing == true ? Event::Outbound : Event::Inbound);
      e.setIsDraft(false);
      e.setIsRead(true);
      e.setIsMissedCall(false);
      e.setIsEmergencyCall(false);
      e.setStatus(Event::DeliveredStatus); // TODO ??
      e.setBytesReceived(bytes_received);
      //      e.setBytesSent(bytes_sent); // TODO ??
      e.setLocalUid(local_uid);
      e.setRemoteUid(remote_uid);
      e.setFreeText(free_text);
      e.setEncoding("UTF-8");             // TODO ??

      e.setMessageToken(QUuid::createUuid().toString()); // TODO ??

      events << e;
    }

    ret = sqlite3_step(stmt);
  }

  if (ret != SQLITE_DONE) {
    qCritical() << sqlite3_errmsg(db);
    return false;
  }

  return true;
}

int main(int argc, char *argv[]) {
  QCoreApplication app(argc, argv);

  if (argc != 2) {
    qCritical() << "Usage:" << argv[0] << "/path/to/el-v1.db";
    return 1;
  }

  GroupFinder f;
  if (!f.init()) {
    qCritical() << "Failed to load groups";
    return 1;
  }

  QList<Event> events;

  if (!readDb(argv[1], events)) {
    return 1;
  }

  EventModel m;
  m.setQueryMode(EventModel::SyncQuery);

  for (int x = 0; x < events.size(); x++) {
    Event& e = events[x];
    QString localUid = ACCOUNT_PREFIX + e.localUid();
    e.setLocalUid(localUid);
    int id = f.groupId(localUid, e.remoteUid());

    if (id == -1) {
      id = f.newGroup(localUid, e.remoteUid());
      if (id == -1) {
	qCritical() << "Failed to add group";
	return 1;
      }
    }
    e.setGroupId(id);
  }

  qCritical() << "I have" << events.size() << "events!";

  if (!m.addEvents(events, false)) {
    qCritical() << "Failed to add events";
    return 1;
  }

  Catcher c(&m);
  c.waitCommit(events.count());

  qCritical() << "Done";

  return 0;
}
