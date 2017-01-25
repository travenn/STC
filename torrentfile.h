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
#include <QFileSystemWatcher>
#include <QDateTime>


//! QRunnable reimplementation to create SHA1 hashes.
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


//! Creates the piece variable for given filelist. FileIO is done in the current thread while hashing is done in a threadpool. @warning Does block and shouldn't be used in the main thread.
class TorrentFileHasher : public QObject
{
    Q_OBJECT
public:
    explicit TorrentFileHasher(const QMap<QString, qint64>& filelist, qint64 piecesize, qint64 contentlength, QObject *parent = 0) : QObject(parent),
        m_filehash(filelist),
        m_piecesize(piecesize),
        m_contentlength(contentlength) {}

private:
    QMap<QString, qint64> m_filehash;
    qint64 m_piecesize, m_contentlength;
    bool m_stop = false;
    QMutex m_mutex;
    QThreadPool m_pool;
    QList<HashTask *> m_hashtasks;

    void throwerror(const QString& msg)
    {
        m_pool.waitForDone(30000);
        for (auto i = m_hashtasks.constBegin(); i != m_hashtasks.constEnd(); ++i)
            delete (*i);
        m_hashtasks.clear();
        emit error(msg);
    }

signals:
    void progressUpdate(int progress);
    void done(QByteArray pieces);
    void error(QString errormessage);

public slots:
    void abort() {m_mutex.lock(); m_stop = true; m_mutex.unlock();}
    void hash()
    {
        int progress = 0;
        qint64 donesize = 0;
        m_pool.setMaxThreadCount(qMax(2, QThread::idealThreadCount()));
        QFile f;
        QByteArray ba, result;
        QStringList filelist = m_filehash.keys();
        int i = -1;
        while (!m_stop)
        {
            if (!f.isOpen())
            {
                ++i;
                if (i == filelist.size())
                    break;
                f.setFileName(filelist.at(i));
                if (!f.open(QIODevice::ReadOnly | QIODevice::Unbuffered))
                {
                    throwerror("Can't open file: " + filelist.at(i));
                    return;
                }
                if (f.size() != m_filehash.value(filelist.at(i)))
                {
                    f.close();
                    throwerror("File \"" + filelist.at(i) + "\"has been changed, operation aborted!");
                    return;
                }
            }
            ba += f.read(m_piecesize - ba.length());
            if (ba.length() == m_piecesize)
            {
                HashTask* h = new HashTask(ba);
                m_hashtasks << h;
                ba.clear();
                donesize += m_piecesize;
                int pg = (double)donesize / (double)m_contentlength *100;
                if (pg != progress)
                {
                    progress = pg;
                    emit progressUpdate(progress);
                }

                while (!m_pool.tryStart(h)) {}
            }
            else
                f.close();
        }

        m_pool.waitForDone();
        if (f.isOpen())
            f.close();

        if (!m_stop)
        {
            for (auto i = m_hashtasks.constBegin(); i != m_hashtasks.constEnd(); ++i)
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
            for (auto i = m_hashtasks.constBegin(); i != m_hashtasks.constEnd(); ++i)
                delete (*i);
            m_hashtasks.clear();
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
    Q_INVOKABLE void setRootDirectory(const QString& path);

    //! Aborts a running hash thread if there is any.
    Q_INVOKABLE void abortHashing();

    //! Creates bencoding of the given data. @param createinfohash true creates and sets the info hash.
    QByteArray encode(const QVariant& data, const bool createinfohash = false);


    Q_INVOKABLE QString getName() const {return m_data.value("info").toMap().value("name").toString();}
    Q_INVOKABLE QStringList getAnnounceUrls() const;
    Q_INVOKABLE QStringList getWebseedUrls() const {return m_data.value("url-list").toStringList();}
    Q_INVOKABLE qint64 getCreationDate() const {return m_data.value("creation date", 0).toLongLong();}
    Q_INVOKABLE qint64 getContentLength() const;
    Q_INVOKABLE QString getComment() const {return m_data.value("comment").toString();}
    QString getCreatedBy() const {return m_data.value("created by").toString();}
    QString getEncoding() const {return m_data.value("encoding").toString();}
    QByteArray getPieces() const {return m_data.value("info").toMap().value("pieces").toByteArray();}
    Q_INVOKABLE qint64 getPieceLength() const {return m_data.value("info").toMap().value("piece length", 0).toLongLong();}
    Q_INVOKABLE bool isPrivate() const {return m_data.value("info").toMap().value("private", false).toBool();}
    //! The hash of the info dictionary also known as Torrent Hash. @warning Keep in mind only the fields actually parsed will be used to create the hash. @sa DATATYPE
    Q_INVOKABLE QByteArray getInfoHash(bool hex = false) const {return hex ? m_infohash.toHex() : m_infohash;}
    //! Returns any additional data. @sa load() @note The structure remains the same, it's basically just stripped of any standard keys. @return QVariant() when there is no additional data.
    QVariant getAdditionalData() const;
    Q_INVOKABLE QString getParentDirectory() const {return m_parentdir;}
    Q_INVOKABLE QString getRealName() const {return m_realname;}

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
    Q_INVOKABLE void setPieceLength(const qint64& bytes);
    Q_INVOKABLE void setPrivate(const bool& is_private);
    //! Adds additional data to the torrent file.
    Q_INVOKABLE void addAdditionalData(const QString& key, const QVariant& value) {if (!standardkeys.contains(key)) m_data.insert(key, value);}
    //! Sets the piece length to the smallest size that doesn't exceed maxpiecenumber or maxpiecesize. @returns piece length.
    Q_INVOKABLE qint64 setAutomaticPieceLength();
    //! Adds current secs since epoch to the info section to alter info hash.
    void dupe();


private:
    QVariantMap m_data;
    QByteArray m_infohash;
    QString m_realname, m_parentdir;
    QThread* m_hashthread = 0;
    TorrentFileHasher* m_hasher = 0;
    QMap<QString, qint64> m_filelist;
    QFileSystemWatcher m_watcher;
    QFile m_outputfile;


    QVariant decodeBencode(const QByteArray& bencode, DATATYPE keytype = ADDITIONAL, qint64 *parsedLength = 0);
    void resetFiles();

signals:
    //! Emitted on progress updates after create() was invoked.
    void progress(int percentage);
    //! Emitted after a torrent file is finished. @sa create()
    void finished(bool success);
    //! Emitted when a directory used to create this torrent had files added / removed.
    void watchedDirChanged(QString dirpath);
    //! Emitted on any error. If errors occured while hashing the error is emitted after the hashing was aborted.
    void error(QString msg);

private slots:
    void onWatchedDirChanged(const QString& dir);

public slots:
    void onThreadFinished(QByteArray pieces);
    void onHashError(QString msg);
};

#endif // TORRENTFILE_H
