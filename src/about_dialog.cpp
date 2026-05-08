#include "about_dialog.h"

#include <ntqlabel.h>
#include <ntqlayout.h>
#include <ntqpushbutton.h>
#include <ntqfont.h>
#include <ntqiconset.h>
#include <ntqcolor.h>

#include "embedded_icons.h"
#include "icon_theme.h"

// Complexity: O(1)
// Dependencies: TQt3 widgets, IconTheme, embedded icons
// Alignment: none required

AboutDialog::AboutDialog(TQWidget* parent, const char* name)
    : TQDialog(parent, name, true)
{
    setFixedSize(600, 340);
    setPaletteBackgroundColor(TQColor(0xff, 0xff, 0xff));

    TQWidget* base = new TQWidget(this);
    base->setGeometry(0, 0, width(), height());
    base->setPaletteBackgroundColor(TQColor(0xff, 0xff, 0xff));

    TQVBoxLayout* mainLay = new TQVBoxLayout(base, 10, 10);

    TQHBoxLayout* topLay = new TQHBoxLayout(0, 0, 10);

    TQLabel* leftImg = new TQLabel(base);
    {
        TQPixmap pm = IconTheme::loadEmbeddedPixmap(about_opensnitchtde_png, (int)about_opensnitchtde_png_len, 0);
        leftImg->setPixmap(pm);
        leftImg->setFixedWidth(260);
        leftImg->setAlignment(TQt::AlignLeft | TQt::AlignTop);
    }
    topLay->addWidget(leftImg, 0);

    TQWidget* rightPane = new TQWidget(base);
    TQVBoxLayout* rightLay = new TQVBoxLayout(rightPane, 0, 8);

    TQLabel* title = new TQLabel("OpenSnitch-tde", rightPane);
    {
        TQFont f = title->font();
        f.setBold(true);
        f.setItalic(true);
        f.setPointSize(f.pointSize() + 10);
        title->setFont(f);
        title->setAlignment(TQt::AlignHCenter | TQt::AlignTop);
    }
    rightLay->addWidget(title, 0);

    TQLabel* text = new TQLabel(rightPane);
    text->setText("Trinity DE interactive application firewall\n"
                  "based on OpenSnitch\n"
                  "( https://github.com/evilsocket/opensnitch )");
    text->setAlignment(TQt::AlignHCenter | TQt::AlignTop);
    rightLay->addWidget(text, 0);

    rightLay->addStretch(1);

    TQLabel* by = new TQLabel("by Seb3773", rightPane);
    {
        TQFont f = by->font();
        f.setItalic(true);
        f.setPointSize(f.pointSize() - 1);
        by->setFont(f);
        by->setAlignment(TQt::AlignRight | TQt::AlignBottom);
    }
    rightLay->addWidget(by, 0);

    topLay->addWidget(rightPane, 1);
    mainLay->addLayout(topLay, 1);

    TQHBoxLayout* btnLay = new TQHBoxLayout(0, 0, 0);
    btnLay->addStretch(1);
    TQPushButton* ok = new TQPushButton("OK", base);
    ok->setDefault(true);
    connect(ok, SIGNAL(clicked()), this, SLOT(accept()));
    btnLay->addWidget(ok);
    btnLay->addStretch(1);
    mainLay->addLayout(btnLay, 0);

    rightPane->raise();
    ok->raise();
}
