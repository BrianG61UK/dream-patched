/******************************************************************************\
 * British Broadcasting Corporation
 * Copyright (c) 2009, 2012
 *
 * Author(s):
 *	 Julian Cable, David Flamand (rewrite)
 *
 * Description: MOT Broadcast Website Viewer
 *
 *
 ******************************************************************************
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
\******************************************************************************/

#include "BWSViewer.h"
#include "../util/Settings.h"
#include "../datadecoding/DataDecoder.h"

#include <QDir>
#include <QFile>
#include <QWebHistory>


#define CACHE_HOST          "127.0.0.1" /* Not an actual server, MUST be set to "127.0.0.1" */

#define ICON_REFRESH        ":/icons/Refresh.png"
#define ICON_STOP           ":/icons/Stop.png"

#undef ENABLE_HACK
#define ENABLE_HACK /* Do we really need these hack unless for vtc trial sample? */


BWSViewer::BWSViewer(CDRMReceiver& rec, CSettings& s, QWidget* parent, Qt::WFlags):
    QDialog(parent), Ui_BWSViewer(),
    nam(this, cache, waitobjs, bAllowExternalContent, strCacheHost),
    receiver(rec), settings(s), decoder(NULL), bHomeSet(false), bPageLoading(false),
    bSaveFileToDisk(false), bRestrictedProfile(false), bAllowExternalContent(true),
    bDirectoryIndexChanged(false), iLastAwaitingOjects(0), strCacheHost(CACHE_HOST)
{
    /* Enable minimize and maximize box for QDialog */
	setWindowFlags(Qt::Window);

    setupUi(this);

    /* Setup webView */
    webView->page()->setNetworkAccessManager(&nam);
    webView->pageAction(QWebPage::OpenLinkInNewWindow)->setVisible(false);
    webView->pageAction(QWebPage::DownloadLinkToDisk)->setVisible(false);
    webView->pageAction(QWebPage::OpenImageInNewWindow)->setVisible(false);
    webView->pageAction(QWebPage::DownloadImageToDisk)->setVisible(false);
 
    /* Update time for color LED */
    LEDStatus->SetUpdateTime(1000);

    /* Update various buttons and labels */
    ButtonClearCache->setEnabled(false);
    LabelTitle->setText("");
    Update();

    /* Connect controls */
    connect(buttonOk, SIGNAL(clicked()), this, SLOT(close()));
    connect(actionClear_Cache, SIGNAL(triggered()), SLOT(OnClearCache()));
    connect(actionClose, SIGNAL(triggered()), SLOT(close()));
    connect(actionAllow_External_Content, SIGNAL(triggered(bool)), SLOT(OnAllowExternalContent(bool)));
    connect(actionRestricted_Profile_Only, SIGNAL(triggered(bool)), SLOT(OnSetProfile(bool)));
    connect(actionSave_File_to_Disk, SIGNAL(triggered(bool)), SLOT(OnSaveFileToDisk(bool)));
    connect(ButtonStepBack, SIGNAL(clicked()), this, SLOT(OnBack()));
    connect(ButtonStepForward, SIGNAL(clicked()), this, SLOT(OnForward()));
    connect(ButtonHome, SIGNAL(clicked()), this, SLOT(OnHome()));
    connect(ButtonStopRefresh, SIGNAL(clicked()), this, SLOT(OnStopRefresh()));
    connect(ButtonClearCache, SIGNAL(clicked()), this, SLOT(OnClearCache()));
    connect(webView, SIGNAL(loadStarted()), this, SLOT(OnWebViewLoadStarted()));
    connect(webView, SIGNAL(loadFinished(bool)), this, SLOT(OnWebViewLoadFinished(bool)));
    connect(webView, SIGNAL(titleChanged(const QString &)), this, SLOT(OnWebViewTitleChanged(const QString &)));
    connect(&Timer, SIGNAL(timeout()), this, SLOT(OnTimer()));
}

BWSViewer::~BWSViewer()
{
}

void BWSViewer::UpdateButtons()
{
    ButtonStepBack->setEnabled(webView->history()->canGoBack());
    ButtonStepForward->setEnabled(webView->history()->canGoForward());
    ButtonHome->setEnabled(bHomeSet);
    ButtonStopRefresh->setEnabled(bHomeSet);
    ButtonStopRefresh->setIcon(QIcon(bPageLoading ? ICON_STOP : ICON_REFRESH));
}

QString BWSViewer::ObjectStr(unsigned int count)
{
    return QString(count > 1 ? tr("objects") : tr("object"));
}

void BWSViewer::UpdateStatus()
{
    unsigned int count, size;
    cache.GetObjectCountAndSize(count, size);
    if (count == 0)
        LabelStatus->setText("");
    else
    {
        QString text(tr("%1 %2 cached, %3 kB"));
        text = text.arg(count).arg(ObjectStr(count)).arg((size+999) / 1000);
        iLastAwaitingOjects = waitobjs;
        if (iLastAwaitingOjects)
            text += tr("  |  %1 %2 pending").arg(iLastAwaitingOjects).arg(ObjectStr(iLastAwaitingOjects));
        if (bAllowExternalContent && webView->url().isValid() && webView->url().host() != strCacheHost)
            text += "  |  " + webView->url().toString();
        LabelStatus->setText(text);
    }
}

void BWSViewer::Update()
{
    UpdateStatus();
    UpdateButtons();
}

void BWSViewer::OnTimer()
{
    CParameter& Parameters = *receiver.GetParameters();
    Parameters.Lock();
    ETypeRxStatus status = Parameters.ReceiveStatus.MOT.GetStatus();
    /* Get current data service */
    const int iCurSelDataServ = Parameters.GetCurSelDataService();
    CService service = Parameters.Service[iCurSelDataServ];
    Parameters.Unlock();

    if (decoder == NULL)
        decoder = receiver.GetDataDecoder();

    switch(status)
    {
    case NOT_PRESENT:
        LEDStatus->Reset();
        break;

    case CRC_ERROR:
        LEDStatus->SetLight(CMultColorLED::RL_RED);
        break;

    case DATA_ERROR:
        LEDStatus->SetLight(CMultColorLED::RL_YELLOW);
        break;

    case RX_OK:
        LEDStatus->SetLight(CMultColorLED::RL_GREEN);
        break;
    }

    if (Changed())
    {
        if (bDirectoryIndexChanged)
        {
            bDirectoryIndexChanged = false;
            if (!bHomeSet)
            {
                bHomeSet = true;
                OnHome();
            }
        }
        Update();
        ButtonClearCache->setEnabled(true);
        actionClear_Cache->setEnabled(true);
    }
    else
    {
        unsigned int iAwaitingOjects = waitobjs;
        if (iLastAwaitingOjects != iAwaitingOjects)
        {
            iLastAwaitingOjects = iAwaitingOjects;
            UpdateStatus();
        }
    }
}

void BWSViewer::OnHome()
{
    webView->load("http://" + strCacheHost);
}

void BWSViewer::OnStopRefresh()
{
    if (bPageLoading)
        webView->stop();
    else
    {
        if (webView->url().isEmpty())
            OnHome();
        else
            webView->reload();
    }
}

void BWSViewer::OnBack()
{
    webView->history()->back();
}

void BWSViewer::OnForward()
{
    webView->history()->forward();
}

void BWSViewer::OnClearCache()
{
    webView->setHtml("");
    webView->history()->clear();
    cache.ClearAll();
    bHomeSet = false;
    bPageLoading = false;
    bDirectoryIndexChanged = false;
    ButtonClearCache->setEnabled(false);
    actionClear_Cache->setEnabled(false);
    Update();
}

void BWSViewer::OnWebViewLoadStarted()
{
    bPageLoading = true;
    QTimer::singleShot(20, this, SLOT(Update()));
}

void BWSViewer::OnWebViewLoadFinished(bool ok)
{
    (void)ok;
    bPageLoading = false;
    QTimer::singleShot(20, this, SLOT(Update()));
}

void BWSViewer::OnWebViewTitleChanged(const QString& title)
{
    LabelTitle->setText("<b>" + title + "</b>");
}

void BWSViewer::OnAllowExternalContent(bool isChecked)
{
    bAllowExternalContent = isChecked;
}

void BWSViewer::OnSetProfile(bool isChecked)
{
    bRestrictedProfile = isChecked;
}

void BWSViewer::OnSaveFileToDisk(bool isChecked)
{
    bSaveFileToDisk = isChecked;
}

void BWSViewer::showEvent(QShowEvent* e)
{
	EVENT_FILTER(e);
    /* Get window geometry data and apply it */
    CWinGeom g;
    settings.Get("BWS", g);
    const QRect WinGeom(g.iXPos, g.iYPos, g.iWSize, g.iHSize);

    if (WinGeom.isValid() && !WinGeom.isEmpty() && !WinGeom.isNull())
        setGeometry(WinGeom);

    bAllowExternalContent = settings.Get("BWS", "allowexternalcontent", bAllowExternalContent);
    actionAllow_External_Content->setChecked(bAllowExternalContent);

    bSaveFileToDisk = settings.Get("BWS", "savefiletodisk", bSaveFileToDisk);
    actionSave_File_to_Disk->setChecked(bSaveFileToDisk);

    bRestrictedProfile = settings.Get("BWS", "restrictedprofile", bRestrictedProfile);
    actionRestricted_Profile_Only->setChecked(bRestrictedProfile);

    CParameter& Parameters = *receiver.GetParameters();
    Parameters.Lock();
    const int iCurSelAudioServ = Parameters.GetCurSelAudioService();
    const uint32_t iAudioServiceID = Parameters.Service[iCurSelAudioServ].iServiceID;
    /* Get current data service */
    const int iCurSelDataServ = Parameters.GetCurSelDataService();
    CService service = Parameters.Service[iCurSelDataServ];
    Parameters.Unlock();

    QString strTitle("MOT Broadcast Website");

    if (service.IsActive())
    {
        /* Do UTF-8 to QString (UNICODE) conversion with the label strings */
        QString strLabel = QString().fromUtf8(service.strLabel.c_str()).trimmed();

        /* Service ID (plot number in hexadecimal format) */
        QString strServiceID = "";

        /* show the ID only if differ from the audio service */
        if ((service.iServiceID != 0) && (service.iServiceID != iAudioServiceID))
        {
            if (strLabel != "")
                strLabel += " ";

            strServiceID = "- ID:" +
                           QString().setNum(long(service.iServiceID), 16).toUpper();
        }

        /* add the description on the title of the dialog */
        if (strLabel != "" || strServiceID != "")
            strTitle += " [" + strLabel + strServiceID + "]";
    }
    setWindowTitle(strTitle);

    /* Update window */
    OnTimer();

    /* Activate real-time timer when window is shown */
    Timer.start(GUI_CONTROL_UPDATE_TIME);
}

void BWSViewer::hideEvent(QHideEvent* e)
{
	EVENT_FILTER(e);
    /* Deactivate real-time timer so that it does not get new pictures */
    Timer.stop();

    /* Save window geometry data */
    QRect WinGeom = geometry();

    CWinGeom c;
    c.iXPos = WinGeom.x();
    c.iYPos = WinGeom.y();
    c.iHSize = WinGeom.height();
    c.iWSize = WinGeom.width();
    settings.Put("BWS", c);

    settings.Put("BWS", "savefiletodisk", bSaveFileToDisk);

    settings.Put("BWS", "restrictedprofile", bRestrictedProfile);

    settings.Put("BWS", "allowexternalcontent", bAllowExternalContent);
}

bool BWSViewer::Changed()
{
    bool bChanged = false;
    if (decoder != NULL)
    {
        CMOTObject obj;

        /* Poll the data decoder module for new object */
        while (decoder->GetMOTObject(obj, CDataDecoder::AT_BROADCASTWEBSITE) == TRUE)
        {
            /* Get the current directory */
            CMOTDirectory MOTDir;
            if (decoder->GetMOTDirectory(MOTDir, CDataDecoder::AT_BROADCASTWEBSITE) == TRUE)
            {
                /* ETSI TS 101 498-1 Section 5.5.1 */

                /* Checks if the DirectoryIndex has values */
                if (MOTDir.DirectoryIndex.size() > 0)
                {
                    QString strNewDirectoryIndex;
                    /* TODO proper profile handling */
                    if(MOTDir.DirectoryIndex.find(UNRESTRICTED_PC_PROFILE) != MOTDir.DirectoryIndex.end())
                        strNewDirectoryIndex =
                            MOTDir.DirectoryIndex[UNRESTRICTED_PC_PROFILE].c_str();
                    else if(MOTDir.DirectoryIndex.find(BASIC_PROFILE) != MOTDir.DirectoryIndex.end())
                        strNewDirectoryIndex =
                            MOTDir.DirectoryIndex[BASIC_PROFILE].c_str();
#ifdef ENABLE_HACK
                    if (strNewDirectoryIndex == "not_here.html") /* Hack needed for vtc trial sample */
	                    strNewDirectoryIndex = "index.html";
#endif
                    if (!strNewDirectoryIndex.isNull())
                        bDirectoryIndexChanged |= cache.SetDirectoryIndex(strNewDirectoryIndex);
                }
            }

            /* Get object name */
            QString strObjName(obj.strName.c_str());

            /* Get ContentType */
            QString strContentType(obj.strMimeType.c_str());
#ifdef ENABLE_HACK
            /* Hack needed for vtc trial sample */
            if (strObjName.endsWith(".stm", Qt::CaseInsensitive) && !strContentType.compare("application/octet-stream", Qt::CaseInsensitive))
                strContentType = "text/html";
#endif 
            /* Add received MOT object to webView */
            cache.AddObject(strObjName, strContentType, obj.Body.vecData);

            /* Store received MOT object on disk */
            if (bSaveFileToDisk)
                SaveMOTObject(strObjName, obj);

            /* Set changed flag */
        	bChanged = true;
        }
    }
    return bChanged;
}

void BWSViewer::SaveMOTObject(const QString& strObjName,
                              const CMOTObject& obj)
{
    const CVector<_BYTE>& vecbRawData = obj.Body.vecData;

    /* Set up save path */
    SetupSavePath(strCurrentSavePath);

    /* Generate safe filename */
    QString strFileName = strCurrentSavePath + VerifyHtmlPath(strObjName);

    /* First, create directory for storing the file (if not exists) */
    CreateDirectories(strFileName);

    /* Open file */
    QFile file(strFileName);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        int i, written, size;
        size = vecbRawData.Size();

        /* Write data */
        for (i = 0, written = 0; size > 0 && written >= 0; i+=written, size-=written)
            written = file.write((const char*)&vecbRawData.at(i), size);

        /* Close the file afterwards */
        file.close();
    }
}

void BWSViewer::SetupSavePath(QString& strSavePath)
{
    CParameter& Parameters = *receiver.GetParameters();
    Parameters.Lock();
    const int iCurSelDataServ = Parameters.GetCurSelDataService();
    const int iServiceID = Parameters.Service[iCurSelDataServ].iServiceID;
    strSavePath = QString::fromUtf8(Parameters.GetDataDirectory("MOT").c_str());
    Parameters.Unlock();

    QString strServiceID;
    strServiceID.setNum(iServiceID, 16).toUpper();
    strServiceID = strServiceID.rightJustified(8, '0');

    strSavePath += strServiceID + "/";
}

//////////////////////////////////////////////////////////////////
// CWebsiteCache implementation

void CWebsiteCache::GetObjectCountAndSize(unsigned int& count, unsigned int& size)
{
    mutex.lock();
        count = objects.size();
        size = total_size;
    mutex.unlock();
}

void CWebsiteCache::ClearAll()
{
    mutex.lock();
        strDirectoryIndex = QString(); /* NULL string, not empty string! */
        objects.clear();
        total_size = 0;
    mutex.unlock();
}

void CWebsiteCache::AddObject(QString strObjName, QString strContentType, CVector<_BYTE>& vecbData)
{
    mutex.lock();
        /* increment id counter, 0 is reserved for error */
        id_counter++; if (!id_counter) id_counter++;

        /* Get the object name */
        strObjName = UrlEncodePath(strObjName);

        /* Erase previous object if any */
        map<QString,CWebsiteObject>::iterator it;
        it = objects.find(strObjName);
        if (it != objects.end())
        {
            total_size -= it->second.data.size();
            objects.erase(it);
        }

        /* Insert the new object */
        objects.insert(pair<QString,CWebsiteObject>(strObjName, CWebsiteObject(id_counter, strContentType, vecbData)));
        total_size += vecbData.Size();
    mutex.unlock();

    /* Signal that a new object is added */
    emit ObjectAdded(strObjName);
}

int CWebsiteCache::GetObjectContentType(const QString& strObjName, QString& strContentType)
{
    int id = 0;
    mutex.lock();
        CWebsiteObject* obj = FindObject(strObjName);
        if (obj)
        {
            id = obj->id;
            strContentType = obj->strContentType;
        }
    mutex.unlock();
    return id;
}

int CWebsiteCache::GetObjectSize(const QString& strObjName, const unsigned int id)
{
    int size = 0;
    mutex.lock();
        CWebsiteObject* obj = FindObject(strObjName);
        if (obj && obj->id == id)
        {
            size = obj->data.size();
        }
    mutex.unlock();
    return size;
}

int CWebsiteCache::CopyObjectData(const QString& strObjName, const unsigned int id, char *buffer, int maxsize, int offset)
{
    int size = -1;
    if (maxsize >= 0 && offset >= 0)
    {
        mutex.lock();
            CWebsiteObject* obj = FindObject(strObjName);
            if (obj && obj->id == id)
            {
                size = obj->data.size();
                if (offset < size)
                {
                    size -= offset;
                    if (size > maxsize)
                        size = maxsize;
                    memcpy(buffer, &obj->data.data()[offset], size);
                }
            }
        mutex.unlock();
    }
    return size;
}

CWebsiteObject* CWebsiteCache::FindObject(const QString& strObjName)
{
    map<QString,CWebsiteObject>::iterator it;
    it = objects.find(strObjName);
    return it != objects.end() ? &it->second : NULL;
}

bool CWebsiteCache::SetDirectoryIndex(const QString strNewDirectoryIndex)
{
    bool bChanged;
    mutex.lock();
        bChanged = strDirectoryIndex != strNewDirectoryIndex;
        if (bChanged)
            strDirectoryIndex = strNewDirectoryIndex;
    mutex.unlock();
    return bChanged;
}

QString CWebsiteCache::GetDirectoryIndex()
{
    mutex.lock();
        QString str = strDirectoryIndex;
    mutex.unlock();
    return str;
}


//////////////////////////////////////////////////////////////////
// CNetworkReplyCache implementation

CNetworkReplyCache::CNetworkReplyCache(QNetworkAccessManager::Operation op,
    const QNetworkRequest& req, CWebsiteCache& cache, CCounter& waitobjs)
    : cache(cache), waitobjs(waitobjs),
    readOffset(0), emitted(false), id(0)
{
    /* ETSI TS 101 498-1 Section 6.2.3 */
    QString strUrl(req.url().toString());
    QString strDirectoryIndex;
    if (IsUrlDirectory(strUrl))
        strDirectoryIndex = cache.GetDirectoryIndex();
    path = UrlEncodePath(strUrl + strDirectoryIndex);

    connect(&cache, SIGNAL(ObjectAdded(QString)), this, SLOT(CheckObject(QString)));
    setOperation(op);
    setRequest(req);
    setUrl(req.url());
    open(QIODevice::ReadOnly);
    QCoreApplication::postEvent(this, new QEvent(QEvent::User));
    waitobjs++;
};

CNetworkReplyCache::~CNetworkReplyCache()
{
    waitobjs--;
};

void CNetworkReplyCache::customEvent(QEvent* event)
{
    if (event->type() == QEvent::User)
        CheckObject(path);
}

qint64 CNetworkReplyCache::bytesAvailable() const
{
    return cache.GetObjectSize(path, id);
}

qint64 CNetworkReplyCache::readData(char * data, qint64 maxSize)
{
    int len = cache.CopyObjectData(path, id, data, maxSize, readOffset);
    if (len > 0)
        readOffset += len;
    return len;
}

void CNetworkReplyCache::CheckObject(QString strObjName)
{
    if (!emitted && path == strObjName)
    {
        QString strContentType;
        unsigned int new_id = cache.GetObjectContentType(path, strContentType);
        if (new_id)
        {
            id = new_id;
            setRawHeader(QByteArray("Content-Type"), QByteArray(strContentType.toUtf8().constData()));
            emitted = true;
            emit readyRead(); /* needed for Qt 4.6 */
            emit finished();
        }
    }
}


//////////////////////////////////////////////////////////////////
// CNetworkAccessManager implementation

QNetworkReply* CNetworkAccessManager::createRequest(Operation op, const QNetworkRequest& req, QIODevice* outgoingData)
{
    if (!bAllowExternalContent || req.url().host() == strCacheHost)
        return new CNetworkReplyCache(op, req, cache, waitobjs);
    else
        return QNetworkAccessManager::createRequest(op, req, outgoingData);
}

