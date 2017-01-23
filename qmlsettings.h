#ifndef QMLSETTINGS_H
#define QMLSETTINGS_H

#include <QObject>
#include <QSettings>
#include <QApplication>
#include <QUrl>
#include <QDir>

//! Just a wrapper for QSettings and helper
class QmlSettings : public QSettings
{
    Q_OBJECT
public:
    QmlSettings(QObject* parent = 0) : QSettings(QApplication::applicationDirPath() + "/STC.ini", QSettings::IniFormat, parent) {}

    //! Removes duplicates and empty strings.
    Q_INVOKABLE QString removeDupes(const QString& s)
    {
        QStringList l = s.split('\n');
        l.removeDuplicates();
        l.removeOne("");
        return l.isEmpty() ? QString() : l.join('\n');
    }
    Q_INVOKABLE void setValue(const QString &key, const QVariant &value) {QSettings::setValue(key, value);}
    Q_INVOKABLE QVariant value(const QString &key, const QVariant &defaultValue = QVariant()) const {return QSettings::value(key, defaultValue);}
    Q_INVOKABLE QString urlToLocalFile(const QUrl& url) const {return url.toLocalFile();}
    Q_INVOKABLE QString toNativeSeparators(const QString& path) const {return QDir::toNativeSeparators(path);}
    Q_INVOKABLE QString getFolderPath(const QString& path) const {return QFileInfo(path).absolutePath() + QDir::separator();}
};

#endif // QMLSETTINGS_H
