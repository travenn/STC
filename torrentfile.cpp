#include "torrentfile.h"

QHash<QString, TorrentFile::DATATYPE> TorrentFile::standardkeys{
    {"pieces", TorrentFile::MINIMAL},
    {"info", TorrentFile::MINIMAL},
    {"announce", TorrentFile::MINIMAL},
    {"piece length", TorrentFile::MINIMAL},
    {"name", TorrentFile::MINIMAL},
    {"length", TorrentFile::MINIMAL},
    {"files", TorrentFile::MINIMAL},
    {"path", TorrentFile::MINIMAL},
    {"announce-list", TorrentFile::STANDARD},
    {"creation date", TorrentFile::STANDARD},
    {"comment", TorrentFile::STANDARD},
    {"created by", TorrentFile::STANDARD},
    {"private", TorrentFile::STANDARD},
    {"encoding", TorrentFile::STANDARD}
};

TorrentFile::TorrentFile(QObject *parent) : QObject(parent)
{
    m_data = QVariantMap{{"info", QVariantMap()}};
}

TorrentFile::TorrentFile(const QVariant &data, QObject *parent) : QObject(parent)
{
    m_data = data.toMap();
}

TorrentFile::TorrentFile(const QString &filename, DATATYPE keytype, QObject *parent) : QObject(parent)
{
    load(filename, keytype);
}

TorrentFile::~TorrentFile()
{
    if (m_hashthread)
    {
        m_hasher->abort();
        m_hashthread->quit();
        m_hashthread->wait();
    }
}

bool TorrentFile::load(const QString &filename, DATATYPE keytype)
{
    QFile f(filename);
    if (f.open(QIODevice::ReadOnly))
    {
        m_data = decodeBencode(f.readAll(), keytype).toMap();
        m_realname = m_data.value("info").toMap().value("name").toString();
        f.close();
        return true;
    }
    return false;
}

bool TorrentFile::create(const QString &filename)
{
    if (m_hashthread)
        return false;

    //validate
    QVariantMap info = m_data.value("info").toMap();
    QStringList filelist;
    if (m_data.value("announce").isNull() || info.value("name").isNull())
        return false;
    if (info.contains("length"))
    {
        filelist << m_rootdir + m_realname;
        if (!QFile::exists(filelist.last()))
            return false;
    }
    else
    {
        QVariantList files = info.value("files").toList();
        if (files.isEmpty())
            return false;
        QString name = m_realname;
        if (!name.endsWith('/')) name += "/";
        for (auto i = files.constBegin(); i != files.constEnd(); ++i)
        {
            filelist << m_rootdir + name + (*i).toMap().value("path").toStringList().join('/');
            if (!QFile::exists(filelist.last()))
                return false;
        }
    }

    if (!getPieceSize())
        setAutomaticPieceSize(getContentLength());

    m_savetorrentfilename = filename;

    m_hasher = new TorrentFileHasher(filelist, getPieceSize(), getContentLength());
    m_hashthread = new QThread(this);
    m_hasher->moveToThread(m_hashthread);
    connect(m_hasher, &TorrentFileHasher::progressUpdate, this, &TorrentFile::progress, Qt::QueuedConnection);
    connect(m_hasher, &TorrentFileHasher::done, this, &TorrentFile::onThreadFinished, Qt::QueuedConnection);
    connect(m_hashthread, &QThread::started, m_hasher, &TorrentFileHasher::hash, Qt::QueuedConnection);
    connect(m_hashthread, &QThread::finished, m_hasher, &TorrentFileHasher::deleteLater, Qt::QueuedConnection);
    m_hashthread->start();

    return true;
}

qint64 TorrentFile::calculateTorrentfileSize()
{
    if (!getPieceSize())
        setAutomaticPieceSize(getContentLength());

    QVariantMap map = m_data;
    QVariantMap m = map.value("info").toMap();
    m.remove("pieces");
    map.insert("info", m);

    QByteArray encoded = encode(map);
    qint64 pbytes = getPieceNumber() * 20;
    return encoded.size() + 8 /*6:pieces*/ + QByteArray::number(pbytes).size() +1 /*<bytenumber>:*/ + pbytes ;
}

qint64 TorrentFile::getPieceNumber()
{
    if (!getContentLength() || !getPieceSize())
        return 0;
    return getContentLength() % getPieceSize() ? getContentLength() / getPieceSize() +1 : getContentLength() / getPieceSize();
}

void TorrentFile::setFile(const QString &filename)
{
    QVariantMap m = m_data.value("info").toMap();
    m.remove("files");
    QFileInfo f(filename);
    m.insert("name", f.fileName());
    m_realname = f.fileName();
    m.insert("length", f.size());
    m_data.insert("info", m);
    m_rootdir = f.absolutePath();
    if (!m_rootdir.endsWith('/'))
        m_rootdir += "/";
}

void TorrentFile::setDirectory(const QString &path)
{
    m_rootdir = path.left(path.lastIndexOf('/', -2) +1);
    QVariantMap m = m_data.value("info").toMap();
    m.remove("length");
    QDir dir(path);
    dir.setFilter(QDir::Files | QDir::NoSymLinks);
    m.insert("name", dir.dirName());
    m_realname = dir.dirName();
    QVariantList files;
    QDirIterator it(dir, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        it.next();
        files.append(QVariantMap{{"length", it.fileInfo().size()}, {"path", dir.relativeFilePath(it.filePath()).split('/')}});
    }
    m.insert("files", files);
    m_data.insert("info", m);
}

void TorrentFile::abortHashing()
{
    if (m_hashthread)
    {
        m_hasher->abort();
        m_hashthread->quit();
        m_hashthread->wait();
        m_hashthread->deleteLater();
        m_hashthread = 0;
    }
}

QStringList TorrentFile::getAnnounceUrls() const
{
    QStringList l = m_data.value("announce-list").toStringList();
    QString a = m_data.value("announce").toString();
    if (!a.isEmpty())
        l.prepend(a);
    return l;
}

qint64 TorrentFile::getContentLength() const
{
    QVariantList files = m_data.value("info").toMap().value("files").toList();
    if (files.isEmpty())
        return m_data.value("info").toMap().value("length", 0).toLongLong();
    else
    {
        qint64 length = 0;
        for (auto i = files.constBegin(); i != files.constEnd(); ++i)
            length += (*i).toMap().value("length", 0).toLongLong();
        return length;
    }
}

QVariant TorrentFile::getAdditionalData() const
{
    QVariantMap v = m_data;
    QVariantMap info = m_data.value("info").toMap();
    QVariantList files = info.value("files").toList();
    QVariantList filesnew;

    for (auto i = files.constBegin(); i != files.constEnd(); ++i)
    {
        QVariantMap fmap = (*i).toMap();
        if (fmap.size() > 2)
        {
            fmap.remove("path");
            fmap.remove("length");
            filesnew.append(fmap);
        }
    }


    QStringList stdk = standardkeys.keys();
    for (auto x = stdk.constBegin(); x != stdk.constEnd(); ++x)
    {
        info.remove((*x));
        v.remove((*x));
    }
    if (!filesnew.isEmpty())
        info.insert("files", filesnew);
    if (!info.isEmpty())
        v.insert("info", info);

    return v.isEmpty() ? QVariant() : v;
}

void TorrentFile::setName(const QString& name)
{
    QVariantMap m = m_data.value("info").toMap();
    m.insert("name", name);
    m_data.insert("info", m);
}

void TorrentFile::setAnnounceUrl(const QString &url)
{
    m_data.remove("announce-list");
    m_data.insert("announce", url);
}

void TorrentFile::setAnnounceUrls(const QStringList &list, const bool &multitier)
{
    m_data.remove("announce-list");
    m_data.remove("announce");
    if (list.isEmpty())
        return;

    m_data.insert("announce", list.first());
    if (list.size() == 1)
        return;

    QVariantList vlist;
    for (auto i = list.constBegin(); i != list.constEnd(); ++i)
    {
        if (multitier)
            vlist << QVariantList{QVariantList{(*i)}};
        else
            vlist << (*i);
    }
    if (!multitier)
        vlist = QVariantList{vlist};

    m_data.insert("announce-list", vlist);
}

void TorrentFile::setAnnounceUrls(const QVariantList& list)
{
    m_data.insert("announce", list.first().toList().first());
    m_data.insert("announce-list", list);
}

void TorrentFile::setWebseedUrls(const QStringList &list)
{
    m_data.remove("url-list");
    if (list.isEmpty())
        return;
    if (list.size() == 1)
        m_data.insert("url-list", list.first());
    else
    {
        QVariantList vlist;
        for (auto i = list.constBegin(); i != list.constEnd(); ++i)
            vlist << (*i);
        m_data.insert("url-list", vlist);
    }
}

void TorrentFile::setPieceSize(const qint64 &bytes)
{
    QVariantMap m = m_data.value("info").toMap();
    m.insert("piece length", bytes);
    m_data.insert("info", m);
}

void TorrentFile::setPrivate(const bool &is_private)
{
    QVariantMap m = m_data.value("info").toMap();
    m.remove("private");
    if (is_private)
        m.insert("private", is_private);
    m_data.insert("info", m);
}

QVariant TorrentFile::decodeBencode(const QByteArray& bencode, DATATYPE keytype, qint64* parsedLength)
{
    bool islist = bencode.at(0) == 'l' ? 1 : 0;
    qint64 pos = 1;
    QVariant ret;
    QVariantMap cmap;
    QVariantList clist;
    QString key;

    while (pos < bencode.size())
    {
        QChar typechar(bencode.at(pos));
        if (typechar.isDigit())
        {
            int idx = bencode.indexOf(':', pos);
            QByteArray ls = bencode.mid(pos, idx - pos);
            int length = ls.toInt();
            QByteArray s = bencode.mid(pos + ls.length() +1, length);
            if (islist)
                clist.append(s);
            else
            {
                if (key.isEmpty())
                    key = s;
                else
                {
                    if (standardkeys.value(key, ADDITIONAL) <= keytype)
                        cmap.insert(key, s);
                    key = "";
                }
            }
            pos += ls.length() + 1 + length;
            continue;
        }

        if (typechar == 'i')
        {
            QByteArray s = bencode.mid(pos +1, bencode.indexOf('e', pos) - pos -1);
            qint64 value = s.toLongLong();
            if (islist)
                clist.append(value);
            else
            {
                if (standardkeys.value(key, ADDITIONAL) <= keytype)
                    cmap.insert(key, value);
                key = "";
            }
            pos += s.length() + 2;
            continue;
        }

        if (typechar == 'l' || typechar == 'd')
        {
            QByteArray s = bencode.mid(pos);
            qint64 pl = 0;
            QVariant v = decodeBencode(s, keytype, &pl);
            if (islist)
                clist.append(v);
            else
            {
                if (key == "info")
                    m_infohash = QCryptographicHash::hash(bencode.mid(pos, pl), QCryptographicHash::Sha1);
                if (standardkeys.value(key, ADDITIONAL) <= keytype)
                    cmap.insert(key, v);
                key = "";
            }
            pos += pl;
            continue;
        }

        if (bencode.at(pos) == 'e')
            break;

        break;
    }

    if (parsedLength)
        *parsedLength = pos +1;

    if (islist)
        ret = clist;
    else
        ret = cmap;
    return ret;
}

QByteArray TorrentFile::encode(const QVariant &data, const bool createinfohash)
{
    switch (data.type())
    {
        default:
        case QMetaType::QString:
        case QMetaType::QByteArray: return QByteArray::number(data.toByteArray().length()) + ":" + data.toByteArray();
        case QMetaType::Bool: return QString("i%1e").arg(data.toInt()).toUtf8();
        case QMetaType::Int:
        case QMetaType::LongLong: return QString("i%1e").arg(data.toString()).toUtf8();
        case QMetaType::QVariantMap:
        {
            QByteArray ret = "d";
            for (auto i = data.toMap().constBegin(); i != data.toMap().constEnd(); ++i)
            {
                QByteArray val = encode(i.value(), createinfohash);
                if (i.key() == "info" && createinfohash)
                    m_infohash = QCryptographicHash::hash(val, QCryptographicHash::Sha1);
                ret += encode(i.key()) + val;
            }
            ret += "e";
            return ret;
        }
        case QMetaType::QVariantList:
        {
            QByteArray ret = "l";
            for (auto i = data.toList().constBegin(); i != data.toList().constEnd(); ++i)
                ret += encode(*i);
            ret += "e";
            return ret;
        }
    }
}

qint64 TorrentFile::setAutomaticPieceSize(const qint64 &contentsize, int maxpiecenumber, qint64 maxpiecesize)
{
    qint64 piecesize = 16*1024;
    qint64 piecenum = contentsize / piecesize;
    while (piecenum > maxpiecenumber && piecesize < maxpiecesize)
    {
        piecesize *= 2;
        piecenum = contentsize / piecesize;
    }
    QVariantMap m = m_data.value("info").toMap();
    m.insert("piece length", piecesize);
    m_data.insert("info", m);
    return piecesize;
}

void TorrentFile::onThreadFinished(QByteArray pieces)
{
    if (m_hashthread)
    {
        m_hashthread->quit();
        m_hashthread->wait(5000);
        m_hashthread->deleteLater();
        m_hashthread = 0;
    }
    QVariantMap m = m_data.value("info").toMap();
    m.insert("pieces", pieces);
    m_data.insert("info", m);
    QFile f(m_savetorrentfilename);
    if (f.open(QIODevice::ReadWrite | QIODevice::Truncate))
    {
        f.write(encode(m_data, 1));
        f.close();
        emit finished(true);
    }
    else
        emit finished(false);
}



