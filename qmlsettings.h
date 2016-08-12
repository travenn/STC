#ifndef QMLSETTINGS_H
#define QMLSETTINGS_H

#include <QObject>
#include <QSettings>
#include <QApplication>

class QmlSettings : public QSettings
{
    Q_OBJECT
public:
    QmlSettings(QObject* parent = 0) : QSettings(QApplication::applicationDirPath() + "/STC.ini", QSettings::IniFormat, parent) {}

    Q_INVOKABLE QString removeDupes(const QString& s)
    {
        QStringList l = s.split('\n');
        l.removeDuplicates();
        l.removeOne("");
        return l.isEmpty() ? QString() : l.join('\n');
    }
    Q_INVOKABLE void setValue(const QString &key, const QVariant &value) {QSettings::setValue(key, value);}
    Q_INVOKABLE QVariant value(const QString &key, const QVariant &defaultValue = QVariant()) const {return QSettings::value(key, defaultValue);}
};

#endif // QMLSETTINGS_H
