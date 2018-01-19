#include "bitstreamversionselector.h"
#include "ui_bitstreamversionselector.h"
#include "model/common/comrom.h"
#include <QDebug>

BitstreamVersionSelector::BitstreamVersionSelector(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::BitstreamVersionSelector)
{
    ui->setupUi(this);
}

BitstreamVersionSelector::~BitstreamVersionSelector()
{
    delete ui;
}

void BitstreamVersionSelector::on_hm40_clicked()
{
    m_iBitstreamVersion = HM40;
}

void BitstreamVersionSelector::on_hm52_clicked()
{
    m_iBitstreamVersion = HM52;
}

void BitstreamVersionSelector::on_hm100_clicked()
{
    m_iBitstreamVersion = HM100;
}

void BitstreamVersionSelector::on_hm120_clicked()
{
    m_iBitstreamVersion = HM120;
}

void BitstreamVersionSelector::on_av1_clicked()
{
    m_iBitstreamVersion = AV1;
}

void BitstreamVersionSelector::showEvent(QShowEvent * event)
{
    /// restore last selection
    codec m_iBitstreamVersion = (codec) g_cAppSetting.value("last_bitstream_version", (int)HM120).toInt();
    switch(m_iBitstreamVersion)
    {
    case HM40:
        ui->hm40->setChecked(true);
        break;
    case HM52:
        ui->hm52->setChecked(true);
        break;
    case HM100:
        ui->hm100->setChecked(true);
        break;
    case HM120:
        ui->hm120->setChecked(true);
        break;
    case AV1:
        ui->av1->setChecked(true);
        break;
    default:
        ui->hm120->setChecked(true);
        break;

    }
    QDialog::showEvent(event);
}

void BitstreamVersionSelector::hideEvent(QHideEvent * event)
{
    /// save current selection
    xSetDecoderVersion();
    g_cAppSetting.setValue("last_bitstream_version", m_iBitstreamVersion);
    QDialog::hideEvent(event);
}

void BitstreamVersionSelector::on_buttonBox_accepted()
{
    xSetDecoderVersion();
}

void BitstreamVersionSelector::xSetDecoderVersion()
{
    if(ui->hm40->isChecked())
    {
        m_iBitstreamVersion = HM40;
    }
    else if(ui->hm52->isChecked() )
    {
        m_iBitstreamVersion = HM52;
    }
    else if(ui->hm100->isChecked() )
    {
        m_iBitstreamVersion = HM100;
    }
    else if(ui->hm120->isChecked() )
    {
        m_iBitstreamVersion = HM120;
    }
    else if(ui->av1->isChecked() )
    {
        m_iBitstreamVersion = AV1;
    }
}
