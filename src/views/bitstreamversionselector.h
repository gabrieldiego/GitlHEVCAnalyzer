#ifndef BITSTREAMVERSIONSELECTOR_H
#define BITSTREAMVERSIONSELECTOR_H

#include <QDialog>
#include "gitldef.h"

namespace Ui {
    class BitstreamVersionSelector;
}

enum codec {
  HM40 = 40,
  HM52 = 52,
  HM100=100,
  HM120=120,
  AV1  =1000
};

class BitstreamVersionSelector : public QDialog
{
    Q_OBJECT

public:
    explicit BitstreamVersionSelector(QWidget *parent = 0);
    ~BitstreamVersionSelector();

    ADD_CLASS_FIELD(int, iBitstreamVersion, getBitstreamVersion, setBitstreamVersion)

    private slots:
    void on_buttonBox_accepted();

    void on_hm40_clicked();
    void on_hm52_clicked();
    void on_hm100_clicked();
    void on_hm120_clicked();

    void on_av1_clicked();

protected:
    virtual void showEvent(QShowEvent * event);
    virtual void hideEvent(QHideEvent * event);
    void xSetDecoderVersion();

private:
    Ui::BitstreamVersionSelector *ui;
};

#endif // BITSTREAMVERSIONSELECTOR_H
