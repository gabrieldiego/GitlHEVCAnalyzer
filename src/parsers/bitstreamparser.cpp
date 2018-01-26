#include "bitstreamparser.h"
#include "../views/bitstreamversionselector.h"
#include "exceptions/decodernotfoundexception.h"
#include "exceptions/bitstreamnotfoundexception.h"
#include "gitlupdateuievt.h"
#include <QProcess>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QApplication>

BitstreamParser::BitstreamParser(QObject *parent):
    m_cDecoderProcess(this)
{
    connect(&m_cDecoderProcess, SIGNAL(readyReadStandardOutput()), this, SLOT(displayDecoderOutput()));
    connect(qApp, SIGNAL(aboutToQuit()), &m_cDecoderProcess, SLOT(kill()));
    //connect
}

BitstreamParser::~BitstreamParser()
{
    m_cDecoderProcess.kill();
    //m_cDecoderProcess.waitForFinished();
}


bool BitstreamParser::parseFile(QString strDecoderFolder,
                                int iEncoderVersion,
                                QString strBitstreamFilePath,
                                QString strOutputPath,
                                ComSequence* pcSequence)
{
    QDir cCurDir = QDir::current();
    /// check if decoder exist
    QString strDecoderPath;
    QStringList cCandidateDecoderList;

    switch( iEncoderVersion )
    {
        case HM40:
        case HM52:
        case HM100:
        case HM120:
            cCandidateDecoderList << QString("HM_%1").arg(iEncoderVersion)
                              << QString("HM_%1.exe").arg(iEncoderVersion)
                              << QString("HM_%1.out").arg(iEncoderVersion);
            break;
        case AV1:
            cCandidateDecoderList << QString("av1");
            break;
        default:
            break;
    }

    QDir cDecoderFolder(strDecoderFolder);
    foreach(const QString& strDecoderExe, cCandidateDecoderList)
    {
        if( cDecoderFolder.exists(strDecoderExe) )
        {
            strDecoderPath = cDecoderFolder.absoluteFilePath(strDecoderExe);
            break;
        }
    }
    /// not found
    if(strDecoderPath.isEmpty())
    {
        qCritical() << QString("Decoder Not found in folder %1").arg(strDecoderFolder);
        throw DecoderNotFoundException();
    }

    /// check if bitstream file exist
    if( (!cCurDir.exists(strBitstreamFilePath)) ||
        (!cCurDir.isAbsolutePath(strBitstreamFilePath)) )
    {
        throw BitstreamNotFoundException();
    }

    /// check if output folder exist
    if( !cCurDir.exists(strOutputPath) )
    {
        cCurDir.mkpath(strOutputPath);
    }

    m_cDecoderProcess.setWorkingDirectory(strOutputPath);
    QString strStandardOutputFile = strOutputPath+"/decoder_general.txt";
    m_cStdOutputFile.setFileName(strStandardOutputFile);
    m_cStdOutputFile.open(QIODevice::WriteOnly);

    /// convert to native path
    strDecoderPath = QDir::toNativeSeparators(strDecoderPath);
    /// convert to native path
    strBitstreamFilePath = QDir::toNativeSeparators(strBitstreamFilePath);

    QString strDecoderCmd;

    switch( iEncoderVersion )
    {
        case HM40:
        case HM52:
        case HM100:
        case HM120:
            strDecoderCmd = QString("\"%1\" -b \"%2\" -o decoder_yuv.yuv").arg(strDecoderPath).arg(strBitstreamFilePath);
            break;
        case AV1:
            strDecoderCmd = QString("\"%1\" --i420 \"%2\" -o decoder_yuv.yuv").arg(strDecoderPath).arg(strBitstreamFilePath);
            break;
        default:
            break;
    }

    qDebug() << strDecoderCmd;

    m_cDecoderProcess.start(strDecoderCmd);
    /// wait for end/cancel
    m_cDecoderProcess.waitForFinished(-1);

    m_cStdOutputFile.close();

    pcSequence->setDecodingFolder(strOutputPath);

    return (m_cDecoderProcess.exitCode() == 0);
}

void BitstreamParser::displayDecoderOutput()
{



    while( m_cDecoderProcess.canReadLine() )
    {
        // write to file
        QString strLine = m_cDecoderProcess.readLine();
        m_cStdOutputFile.write(strLine.toStdString().c_str());


        // display progress text as event
        QRegExp cMatchTarget;
        cMatchTarget.setPattern("POC *(-?[0-9]+)");

        if( cMatchTarget.indexIn(strLine) != -1 )
        {
            GitlUpdateUIEvt evt;
            int iPoc = cMatchTarget.cap(1).toInt();
            QString strText = QString("POC %1 Decoded").arg(iPoc);
            evt.setParameter("decoding_progress", strText);
            dispatchEvt(evt);
        }
    }

}
