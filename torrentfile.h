#ifndef TORRENTFILE_H
#define TORRENTFILE_H

#include <QObject>
#include <QFile>
#include <QCryptographicHash>
#include <QVariant>
#include <QFileInfo>
#include <QDir>
#include <QDirIterator>
#include <QRunnable>
#include <QThreadPool>
#include <QMutex>



class HashTask : public QRunnable
{
public:
    explicit HashTask(QByteArray data) : m_data(data) {setAutoDelete(false);}
    QByteArray m_data, result;
    void run()
    {
        result = QCryptographicHash::hash(m_data, QCryptographicHash::Sha1);
        m_data.clear();
    }
};

//! Creates the pieces hashbytes for filelist in a threadpool
class TorrentFileHasher : public QObject
{
    Q_OBJECT
public:
    explicit TorrentFileHasher(const QStringList& filelist, qint64 piecesize, qint64 contentlength, QObject *parent = 0) : QObject(parent),
        m_filelist(filelist),
        m_piecesize(piecesize),
        m_contentlength(contentlength) {}

private:
    QStringList m_filelist;
    qint64 m_piecesize, m_contentlength;
    bool m_stop = false;
    QMutex m_mutex;

signals:
    void progressUpdate(int progress);
    void done(QByteArray pieces);

public slots:
    void abort() {m_mutex.lock(); m_stop = true; m_mutex.unlock();}
    void hash()
    {
        int progress = 0;
        qint64 donesize = 0;

        QThreadPool p;
        p.setMaxThreadCount(qMax(2, QThread::idealThreadCount()));
        QList<HashTask *> hashtasks;

        QFile f;
        QByteArray ba, result;
        int i = -1;
        while (!m_stop)
        {
            if (!f.isOpen())
            {
                ++i;
                if (i == m_filelist.size())
                    break;
                f.setFileName(m_filelist.at(i));
                f.open(QIODevice::ReadOnly | QIODevice::Unbuffered);
            }
            ba += f.read(m_piecesize - ba.length());
            if (ba.length() == m_piecesize)
            {
                HashTask* h = new HashTask(ba);
                hashtasks << h;
                ba.clear();
                donesize += m_piecesize;
                int pg = (double)donesize / (double)m_contentlength *100;
                if (pg != progress)
                {
                    progress = pg;
                    emit progressUpdate(progress);
                }

                while (!p.tryStart(h)) {}
            }
            else
                f.close();
        }

        p.waitForDone();

        if (!m_stop)
        {
            for (auto i = hashtasks.constBegin(); i != hashtasks.constEnd(); ++i)
            {
                result += (*i)->result;
                delete (*i);
            }
            if (!ba.isEmpty())
                result += QCryptographicHash::hash(ba, QCryptographicHash::Sha1);
            if (progress != 100) progressUpdate(100);

            emit done(result);
        }
        else
        {

            for (auto i = hashtasks.constBegin(); i != hashtasks.constEnd(); ++i)
                delete (*i);
            hashtasks.clear();
        }
    }
};


//! Reads and writes torrent files. Pretty much a simple De-/Encoder for torrent files. The underlying data is stored in a QVariantMap.
class TorrentFile : public QObject
{
    Q_OBJECT
public:
    //! Enum used for loading torrent files. \li Minimal: Only use the minimum required values without optional fields. \li Standard: Includes minimal as well as optional fields. \li Additional: Allows for any arbitrary field.
    enum DATATYPE {MINIMAL, STANDARD, ADDITIONAL};


    explicit TorrentFile(QObject *parent = 0);
    TorrentFile(const QVariant& data, QObject *parent = 0);
    TorrentFile(const QString& filename, DATATYPE keytype = ADDITIONAL, QObject *parent = 0);
    ~TorrentFile();

    //! The standard torrent keys
    static QHash<QString, DATATYPE> standardkeys;

    //! Opens and parses keys specified by keytype of the given file.
    Q_INVOKABLE bool load(const QString& filename, DATATYPE keytype = ADDITIONAL);

    //! Creates a torrentfile from the data. The hashing is done in a seperate thread. @sa TorrentFileHashCreator, progress(), finished() @return false if there is data missing, otherwise true.
    Q_INVOKABLE bool create(const QString& filename);

    //! Returns the size the resulting torrent file will be in bytes.
    Q_INVOKABLE qint64 calculateTorrentfileSize();

    //! Returns the number of pieces.
    Q_INVOKABLE qint64 getPieceNumber();

    //! Returns the torrents data as QVariant. You can parse this map for additional / non standard elements.
    Q_INVOKABLE QVariant toVariant() const {return m_data;}

    //! Sets the file for single file torrents.
    Q_INVOKABLE void setFile(const QString& filename);

    //! Sets the directory for multi-file torrents.
    Q_INVOKABLE void setDirectory(const QString& path);

    //! Sets the directory where the files specified in the metainfo are in. This is needed to find the files when creating a torrent file.
    Q_INVOKABLE void setRootDirectory(const QString& path) {m_rootdir = path; if (!m_rootdir.endsWith('/')) m_rootdir += "/";}

    //! Aborts a running hash thread if there is any.
    Q_INVOKABLE void abortHashing();


    Q_INVOKABLE QString getName() const {return m_data.value("info").toMap().value("name").toString();}
    Q_INVOKABLE QStringList getAnnounceUrls() const;
    Q_INVOKABLE QStringList getWebseedUrls() const {return m_data.value("url-list").toStringList();}
    Q_INVOKABLE qint64 getCreationDate() const {return m_data.value("creation data", 0).toLongLong();}
    Q_INVOKABLE qint64 getContentLength() const;
    Q_INVOKABLE QString getComment() const {return m_data.value("comment").toString();}
    QString getCreatedBy() const {return m_data.value("created by").toString();}
    QString getEncoding() const {return m_data.value("encoding").toString();}
    QByteArray getPieces() const {return m_data.value("info").toMap().value("pieces").toByteArray();}
    Q_INVOKABLE qint64 getPieceSize() const {return m_data.value("info").toMap().value("piece length", 0).toLongLong();}
    Q_INVOKABLE bool isPrivate() const {return m_data.value("info").toMap().value("private", false).toBool();}
    //! The hash of the info dictionary also known as Torrent Hash. @warning Keep in mind only the fields actually parsed will be used to create the hash. @sa DATATYPE
    Q_INVOKABLE QByteArray getInfoHash(bool hex = false) const {return hex ? m_infohash.toHex() : m_infohash;}
    //! Returns any additional data. @sa load() @note The structure remains the same, it's basically just stripped of any standard keys. @return QVariant() when there is no additional data.
    QVariant getAdditionalData() const;

    Q_INVOKABLE void setName(const QString& name);
    //! Sets the announce url for a single tracker torrent.
    void setAnnounceUrl(const QString& url);
    //! Sets urls for multi tracker torrent. @param multitier: \li true: every tracker is it's own tier. \li false: All tier 1.
    Q_INVOKABLE void setAnnounceUrls(const QStringList& list, const bool& multitier = true);
    //! Overload, lets you control the tiers. See http://bittorrent.org/beps/bep_0012.html
    void setAnnounceUrls(const QVariantList& list);
    Q_INVOKABLE void setWebseedUrls(const QStringList& list);
    Q_INVOKABLE void setCreationDate(const qint64& secssinceepoch) {if (!secssinceepoch) m_data.remove("creation date"); else m_data.insert("creation date", secssinceepoch);}
    Q_INVOKABLE void setComment(const QString& comment) {if (comment.isEmpty()) m_data.remove("comment"); else m_data.insert("comment", comment);}
    Q_INVOKABLE void setCreatedBy(const QString& creator) {if (creator.isEmpty()) m_data.remove("created by"); else m_data.insert("created by", creator);}
    //! Must be a power of 2. When 0(default) it will be automatically calculated when creating a torrent.
    Q_INVOKABLE void setPieceSize(const qint64& bytes);
    Q_INVOKABLE void setPrivate(const bool& is_private);
    //! Adds additional data to the torrent file.
    Q_INVOKABLE void addAdditionalData(const QString& key, const QVariant& value) {if (!standardkeys.contains(key)) m_data.insert(key, value);}
    //! Sets the piece length to the smallest size that doesn't exceed maxpiecenumber or maxpiecesize. @returns piece length.
    Q_INVOKABLE qint64 setAutomaticPieceSize(const qint64& contentsize, int maxpiecenumber = 5000, qint64 maxpiecesize = 16777216);

private:
    QVariantMap m_data;
    QByteArray m_infohash;
    QString m_rootdir, m_realname, m_savetorrentfilename;
    QThread* m_hashthread = 0;
    TorrentFileHasher* m_hasher = 0;

    QVariant decodeBencode(const QByteArray& bencode, DATATYPE keytype = ADDITIONAL, qint64 *parsedLength = 0);
    QByteArray encode(const QVariant& data);

signals:
    //! Emitted on progress updates after create() was invoked.
    void progress(int percentage);
    //! Emitted after a torrent file is finished. @sa create()
    void finished(bool success);

public slots:
    void onThreadFinished(QByteArray pieces);
};

#endif // TORRENTFILE_H
