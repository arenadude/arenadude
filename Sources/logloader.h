#ifndef LOGLOADER_H
#define LOGLOADER_H

#include "utility.h"
#include "logworker.h"
#include <QObject>
#include <QTextStream>

#define MIN_UPDATE_TIME 500
#define MAX_UPDATE_TIME 2000
#define UPDATE_TIME_STEP 500
#define LOG_DIR_TIME_CHECK 15000


class DataLog
{
public:
    LogComponent logComponent;
    QString line;
    qint64 numLine;
    qint64 logSeek;
};


class LogLoader : public QObject
{
    Q_OBJECT

//Constructor
public:
    LogLoader(QObject *parent);
    ~LogLoader();

//Variables
private:
    bool hearthstoneRestartNeeded = false;
    QString logsDirPath, logConfig, recentLogDir;
    QMap<QString, LogWorker *>logWorkerMap;
    QList<QString> logComponentList;
    int updateTime, maxUpdateTime;
    bool sortLogs, synchronized;
    bool firstRunPending = false;
    QMap<qint64,DataLog> dataLogs;
    //Log lines only have the time of day: the day is counted from the session start (log dir name) and from
    //each log's times going back at midnight, so lines after midnight sort after the ones before
    qint64 sessionStartTime = -1;           //Time of day, 1e-7 s units
    QMap<int, qint64> lastLineTime;         //By LogComponent
    QMap<int, int> lineDay;                 //By LogComponent
    QRegularExpressionMatch *match;

//Metodos
private:
    bool readSettings();
    bool readLogsDirPath();
    bool readLogConfigPath();
    QString createDefaultLogConfig();
    bool checkLogConfig();
    bool checkLogConfigOption(QString option, QString &data, QTextStream &stream);
    void setMaxUpdateTime(int value);
    void createLogWorkers();
    void createLogWorker(QString logComponent);
    void addToDataLogs(LogComponent logComponent, QString line, qint64 numLine, qint64 logSeek);
    void processDataLogs();
    void deleteLogWorkers();
    void removeOldLogDirs(QStringList logs);

public:
    bool isHearthstoneRestartNeeded();
    bool init();
    QString getLogConfigPath();
    QString getRecentLogDir();

//Signals
signals:
    void logReset();
    void logsCaughtUp();    //The log lines written before the tracker started are processed
    void logConfigSet();
    void pDebug(QString line, DebugLevel debugLevel=Normal, QString file="LogLoader");

    //LogWorker signal reemit
    void newLogLineRead(LogComponent logComponent, QString line, qint64 numLine, qint64 logSeek);


//Slots
private slots:
    //LogWorker signal reemit
    void emitNewLogLineRead(LogComponent logComponent, QString line, qint64 numLine, qint64 logSeek);
    void sendLogWorker();
    void sendLogWorkerFirstRun();
    void checkLogDir();

public slots:
    void setUpdateTimeMax();
    void setUpdateTimeMin();
};

#endif // LOGLOADER_H
