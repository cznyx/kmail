// kmreaderwin.cpp
// Author: Markus Wuebben <markus.wuebben@kde.org>

#include <qdir.h>
#include <kfiledialog.h>
#include <stdlib.h>
#include <sys/stat.h>

#ifndef KRN
#include "kmglobal.h"
#include "kmmainwin.h"
#else
#endif

#include "kmmessage.h"
#include "kmmsgpart.h"
#include "kmreaderwin.h"
#include "kfileio.h"
#include "kbusyptr.h"
#include "kmmsgpartdlg.h"
#include "kpgp.h"
#include "kfontutils.h"
#include "kurl.h"

#include <khtml_part.h>
#include <kapp.h>
#include <kconfig.h>
#include <kcursor.h>
#include <krun.h>
#include <kopenwith.h>
#include <kmessagebox.h>
#include <mimelib/mimepp.h>
#include <qstring.h>
#include <errno.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <qbitmap.h>
#include <qclipboard.h>
#include <qcursor.h>
#include <qmultilineedit.h>
#include <qregexp.h>
#include <qscrollbar.h>

// for selection
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

//--- Sven's save attachments to /tmp start ---
#include <unistd.h>
#include <klocale.h>
#include <kglobal.h>
#include <kstddirs.h>  // for access and getpid
//--- Sven's save attachments to /tmp end ---

// Do the tmp stuff correctly - thanks to Harri Porten for
// reminding me (sven)

#ifdef HAVE_PATHS_H
#include <paths.h>
#endif

#ifdef KRN
extern KApplication *app;
extern KBusyPtr *kbp;
#endif

QString KMReaderWin::mAttachDir;
const int KMReaderWin::delay = 150;

//-----------------------------------------------------------------------------
KMReaderWin::KMReaderWin(QWidget *aParent, const char *aName, int aFlags)
  :KMReaderWinInherited(aParent, aName, aFlags)
{
  initMetaObject();

  mAutoDelete = FALSE;
  mMsg = 0;

  initHtmlWidget();
  readConfig();
  mHtmlOverride = false;

  if (mAttachDir.isNull()) makeAttachDir();
  connect(&updateReaderWinTimer, SIGNAL(timeout()),
  	  this,SLOT(updateReaderWin()));
}


//-----------------------------------------------------------------------------
KMReaderWin::~KMReaderWin()
{
  delete mViewer;  //hack to prevent segfault on exit
  if (mAutoDelete) delete mMsg;
}


//-----------------------------------------------------------------------------
void KMReaderWin::makeAttachDir(void)
{
  QString directory;
  directory.sprintf("kmail/tmp/kmail%d/", getpid());
  KGlobal::dirs()->
    addResourceType("kmail_tmp",
		    KStandardDirs::kde_default("data") + directory);
  mAttachDir = locateLocal( "kmail_tmp", "/" );

  if (mAttachDir.isNull()) warning(i18n("Failed to create temporary "
					"attachment directory '%s': %s"),
				   directory.ascii(), strerror(errno));
}



//-----------------------------------------------------------------------------
void KMReaderWin::readConfig(void)
{
  KConfig *config = kapp->config();

  config->setGroup("Pixmaps");
  mBackingPixmapOn = FALSE;
  mBackingPixmapStr = config->readEntry("Readerwin","");
  if (mBackingPixmapStr != "")
    mBackingPixmapOn = TRUE;

  config->setGroup("Reader");
  mHtmlMail = config->readBoolEntry( "htmlMail", true );
  mAtmInline = config->readNumEntry("attach-inline", 100);
  mHeaderStyle = (HeaderStyle)config->readNumEntry("hdr-style", HdrFancy);
  mAttachmentStyle = (AttachmentStyle)config->readNumEntry("attmnt-style",
							   SmartAttmnt);
#ifdef KRN
  config->setGroup("ArticleListOptions");
#endif
  c1 = QColor(kapp->palette().normal().text());
  c2 = QColor("blue");
  c3 = QColor("red");
  c4 = QColor(kapp->palette().normal().base());

  if (!config->readBoolEntry("defaultColors",TRUE)) {
    c1 = config->readColorEntry("ForegroundColor",&c1);
    c2 = config->readColorEntry("LinkColor",&c2);
    c3 = config->readColorEntry("FollowedColor",&c3);
    c4 = config->readColorEntry("BackgroundColor",&c4);
    // ### FIXME: stylesheet
    //        mViewer->setDefaultBGColor(c4);
    //        mViewer->setDefaultTextColors(c1,c2,c3);
  }
  else {
  // ### FIXME: stylesheet
  //    mViewer->setDefaultBGColor(c4);
  //    mViewer->setDefaultTextColors(c1,c2,c3);
  }

  mRecyleQouteColors = config->readBoolEntry( "RecycleQuoteColors", false );

#ifndef KRN
  int i, diff;
  fntSize = 0;
  
  config->setGroup("Fonts");
  if (!config->readBoolEntry("defaultFonts",TRUE)) {
    mBodyFont = config->readEntry("body-font", "helvetica-medium-r-12");
    mViewer->setStandardFont(kstrToFont(mBodyFont).family());
    fntSize = kstrToFont(mBodyFont).pointSize();
    mBodyFamily = kstrToFont(mBodyFont).family();
  }
  else {
    setFont(KGlobal::generalFont());
    fntSize = KGlobal::generalFont().pointSize();
    mBodyFamily = KGlobal::generalFont().family();
  }

  QValueList<int> fontsizes;
  mViewer->resetFontSizes();
  diff = fntSize - mViewer->fontSizes()[3];
  if (mViewer->fontSizes()[0]+diff > 0) {
    for (i=0;i<7; i++)
      fontsizes << mViewer->fontSizes()[i] + diff;
    mViewer->setFontSizes(fontsizes);
  }

#else
  mViewer->setDefaultFontBase(config->readNumEntry("DefaultFontBase",3));
  mViewer->setStandardFont(config->readEntry("StandardFont","helvetica"));
  mViewer->setFixedFont(config->readEntry("FixedFont","courier"));
#endif

  //
  // Prepare the quoted fonts
  //
  mQuoteFontTag[0] = quoteFontTag(0);
  mQuoteFontTag[1] = quoteFontTag(1);
  mQuoteFontTag[2] = quoteFontTag(2);


  if (mMsg)
    update();
}



//-----------------------------------------------------------------------------
void KMReaderWin::writeConfig(bool aWithSync)
{
  KConfig *config = kapp->config();

  config->setGroup("Reader");
  config->writeEntry("attach-inline", mAtmInline);
  config->writeEntry("hdr-style", (int)mHeaderStyle);
  config->writeEntry("attmnt-style",(int)mAttachmentStyle);

  if (aWithSync) config->sync();
}




QString KMReaderWin::quoteFontTag( int quoteLevel )
{
  KConfig &config = *kapp->config();
 
  QColor color;
  config.setGroup("Reader");
  if( config.readBoolEntry( "defaultColors", true ) == true )
  {
    color = QColor(kapp->palette().normal().text());
  }
  else
  {
    QColor defaultColor = QColor(kapp->palette().normal().text());
    if( quoteLevel == 0 )
      color = config.readColorEntry( "QuoutedText1", &defaultColor );
    else if( quoteLevel == 1 )
      color = config.readColorEntry( "QuoutedText2", &defaultColor );
    else if( quoteLevel == 2 )
      color = config.readColorEntry( "QuoutedText3", &defaultColor );
    else
      color = QColor(kapp->palette().normal().base());
  }

  QFont font;
  config.setGroup("Fonts");
  if( config.readBoolEntry( "defaultFonts", true ) == true )
  {
    font = KGlobal::generalFont();
    font.setItalic(true);
  }
  else
  {
    const char *defaultFont = "helvetica-medium-r-12";
    if( quoteLevel == 0 )
      font  = kstrToFont(config.readEntry( "quote1-font", defaultFont ) );
    else if( quoteLevel == 1 )
      font  = kstrToFont(config.readEntry( "quote2-font", defaultFont ) );
    else if( quoteLevel == 2 )
      font  = kstrToFont(config.readEntry( "quote3-font", defaultFont ) );    
    else
    {
      font = KGlobal::generalFont();
      font.setItalic(true);
    }
  }

  QString str = QString("<font color=\"%1\">").arg( color.name() );
  if( font.italic() ) { str += "<i>"; }
  if( font.bold() ) { str += "<b>"; }
  return( str );
}








//-----------------------------------------------------------------------------
void KMReaderWin::initHtmlWidget(void)
{
  mViewer = new KHTMLPart(this, "khtml");
  mViewer->widget()->resize(width()-16, height()-110);
  mViewer->setURLCursor(KCursor::handCursor());
  //  mViewer->setDefaultBGColor(QColor("#ffffff"));
  //  mViewer->setFollowsLinks( FALSE );

  // ### FIXME
  connect(mViewer->browserExtension(),SIGNAL(openURLRequest(const KURL &, const KParts::URLArgs &)),this,
  	  SLOT(slotUrlOpen(const KURL &, const KParts::URLArgs &)));
  connect(mViewer,SIGNAL(onURL(const QString &)),this,
	  SLOT(slotUrlOn(const QString &)));
    connect(mViewer,SIGNAL(popupMenu(const QString &, const QPoint &)),
	    SLOT(slotUrlPopup(const QString &, const QPoint &)));
  // ### FIXME
  //  connect(mViewer,SIGNAL(textSelected(bool)),
  //          SLOT(slotTextSelected(bool)));

  // ### FIXME
  //connect(mViewer, SIGNAL(documentChanged()), SLOT(slotDocumentChanged()));
  //connect(mViewer, SIGNAL(documentDone()), SLOT(slotDocumentDone()));
}


//-----------------------------------------------------------------------------
void KMReaderWin::setBodyFont(const QString aFont)
{
  mBodyFont = aFont.copy();
  update(true);
}


//-----------------------------------------------------------------------------
void KMReaderWin::setHeaderStyle(KMReaderWin::HeaderStyle aHeaderStyle)
{
  mHeaderStyle = aHeaderStyle;
  update(true);
}


//-----------------------------------------------------------------------------
void KMReaderWin::setAttachmentStyle(int aAttachmentStyle)
{
  mAttachmentStyle = (AttachmentStyle)aAttachmentStyle;
  update(true);
}

//-----------------------------------------------------------------------------
void KMReaderWin::setInlineAttach(int aAtmInline)
{
  mAtmInline = aAtmInline;
  update(true);
}


//-----------------------------------------------------------------------------
void KMReaderWin::setMsg(KMMessage* aMsg, bool force)
{
  // If not forced and there is aMsg and aMsg is same as mMsg then return
  if (!force && aMsg && mMsg == aMsg)
    return;

  mMsg = aMsg;

  // Avoid flicker, somewhat of a cludge
  if (force) {
    mMsgBuf = 0;
    updateReaderWin();
  }
  else if (updateReaderWinTimer.isActive())
    updateReaderWinTimer.changeInterval( delay );
  else {
    //    updateReaderWin();
    updateReaderWinTimer.start( delay, TRUE );
  }
}


//-----------------------------------------------------------------------------
void KMReaderWin::updateReaderWin()
{
  if (mMsgBuf == mMsg)
    return;

  if (mMsg) parseMsg();
  else
  {
    mViewer->begin( KURL( "file:/" ) );
    mViewer->write("<html><body" +
		   QString(" bgcolor=\"#%1\"").arg(colorToString(c4)));

    if (mBackingPixmapOn)
      mViewer->write(" background=\"file://" + mBackingPixmapStr + "\"");
    mViewer->write("></body></html>");
    mViewer->end();
  }

  mMsgBuf = mMsg;
}

QString KMReaderWin::colorToString(const QColor& c)
{
  return QString::number(0x1000000 +
			 (c.red() << 16) +
			 (c.green() << 8) +
			 c.blue(), 16 ).mid(1);
}

//-----------------------------------------------------------------------------
void KMReaderWin::parseMsg(void)
{
  if(mMsg == NULL)
    return;

  QString bkgrdStr = "";
  if (mBackingPixmapOn)
    bkgrdStr = " background=\"file://" + mBackingPixmapStr + "\"";

  mViewer->begin( KURL( "file:/" ) );
  mViewer->write("<html><head><style type=\"text/css\">" +
		 QString("a { color: #%1;").arg(colorToString(c2)) +
		 "text-decoration: none; }" + // just playing
		 "</style></head><body " +
		 // TODO: move these to stylesheet, too:
                 QString(" text=\"#%1\"").arg(colorToString(c1)) +
  		 QString(" bgcolor=\"#%1\"").arg(colorToString(c4)) + 
                 bkgrdStr + ">" );

#if defined CHARSETS
  printf("Setting viewer charset to %s\n",(const char *)mMsg->charset());
  mViewer->setCharset(mMsg->charset());
#endif

  parseMsg(mMsg);

  mViewer->write("</body></html>");
  mViewer->end();
}


//-----------------------------------------------------------------------------
void KMReaderWin::parseMsg(KMMessage* aMsg)
{
  KMMessagePart msgPart;
  int i, numParts;
  QString type, subtype, str, contDisp;
  bool asIcon = false;
  inlineImage = false;

  assert(aMsg!=NULL);
  type = aMsg->typeStr();
  numParts = aMsg->numBodyParts();

  writeMsgHeader();
  if (numParts > 0)
  {
    // ---sven: handle multipart/alternative start ---
    // This is for multipart/alternative messages WITHOUT attachments
    // main header has type=multipart/alternative and one attachment is
    // text/html
    if (type.find("multipart/alternative") != -1 && numParts == 2)
    {
      debug("Alternative message, type: %s",type.data());
      //Now: Only two attachments one of them is html
      for (i=0; i<2; i++)                   // count parts...
      {
        aMsg->bodyPart(i, &msgPart);        // set part...
        subtype = msgPart.subtypeStr();     // get subtype...
        if (htmlMail() && stricmp(subtype, "html")==0)    // is it html?
        {                                   // yes...
          str = QCString(msgPart.bodyDecoded());      // decode it...
          mViewer->write(str);              // write it...
          return;                           // return, finshed.
        }
	else if (!htmlMail() && (stricmp(subtype, "plain")==0))    
	                                    // wasn't html show only if
	{                                   // support for html is turned off
          str = QCString(msgPart.bodyDecoded());      // decode it...
          writeBodyStr(str);
          return;
	}
      }                                     
      // if we are here we didnt find any html part. Handle it normaly then
    }
    // This works only for alternative msgs without attachments. Alternative
    // messages with attachments are broken with or without this. No need
    // to bother with strib </body> or </html> here, because if any part
    // follows this will not be shown correctly. You'll still be able to read the
    // main message and deal with attachments. Nothing I can do now :-(
    // ---sven: handle multipart/alternative end ---

    for (i=0; i<numParts; i++)
    {
      aMsg->bodyPart(i, &msgPart);
      type = msgPart.typeStr();
      subtype = msgPart.subtypeStr();
      contDisp = msgPart.contentDisposition();
      debug("type: %s",type.data());
      debug("subtye: %s",subtype.data());
      debug("contDisp %s",contDisp.data());

      if (i <= 0) asIcon = FALSE;
      else switch (mAttachmentStyle)
      {
      case IconicAttmnt:
        asIcon=TRUE; break;
      case InlineAttmnt:
        asIcon=FALSE; break;
      case SmartAttmnt:
        asIcon=(contDisp.find("inline")<0);
      }

      if (!asIcon)
      {
//	if (i<=0 || stricmp(type, "text")==0)//||stricmp(type, "message")==0)
//	if (stricmp(type, "text")==0)//||stricmp(type, "message")==0)
	if ((type == "") || (stricmp(type, "text")==0))
	{
	  str = QCString(msgPart.bodyDecoded());
	  if (i>0) mViewer->write("<br><hr><br>");

	  if (htmlMail() && (stricmp(subtype, "html")==0))
          {
            // ---Sven's strip </BODY> and </HTML> from end of attachment start-
            // We must fo this, or else we will see only 1st inlined html attachment
            // It is IMHO enough to search only for </BODY> and put \0 there.
            int i;
            i = str.findRev("</body>", -1, false); //case insensitive
            if (i>0)
              str.truncate(i);
            else // just in case - search for </html>
            {
              i = str.findRev("</html>", -1, false); //case insensitive
              if (i>0) str.truncate(i);
            }
            // ---Sven's strip </BODY> and </HTML> from end of attachment end-
            mViewer->write(str);
	  }
          else writeBodyStr(str);
	}
        // ---Sven's view smart or inline image attachments in kmail start---
        else if (stricmp(type, "image")==0)
        {
          inlineImage=true;
          writePartIcon(&msgPart, i);
          inlineImage=false;
        }
        // ---Sven's view smart or inline image attachments in kmail end---
	else asIcon = TRUE;
      }
      if (asIcon)
      {
        writePartIcon(&msgPart, i);
      }
    }
  }
  else // if numBodyParts <= 0
  {
    if (htmlMail() && (type.find("text/html;") != -1))
      mViewer->write(aMsg->bodyDecoded());
    else
      writeBodyStr(aMsg->bodyDecoded());
  }
}


//-----------------------------------------------------------------------------
void KMReaderWin::writeMsgHeader(void)
{
  QString str;

  switch (mHeaderStyle)
  {
    case HdrBrief:
    mViewer->write("<font size=\"+1\"><b>" + strToHtml(mMsg->subject()) +
                   "</b></font>&nbsp; (" +
                   KMMessage::emailAddrAsAnchor(mMsg->from(),TRUE) + ", ");
    if (!mMsg->cc().isEmpty())
      mViewer->write(i18n("Cc: ")+
                     KMMessage::emailAddrAsAnchor(mMsg->cc(),TRUE) + ", ");
    mViewer->write(strToHtml(mMsg->dateShortStr()) + ")<br>\n");
    break;

  case HdrStandard:
    mViewer->write("<font size=\"+1\"><b>" +
                   strToHtml(mMsg->subject()) + "</b></font><br>\n");
    mViewer->write(i18n("From: ") +
                   KMMessage::emailAddrAsAnchor(mMsg->from(),FALSE) + "<br>\n");
    mViewer->write(i18n("To: ") +
                   KMMessage::emailAddrAsAnchor(mMsg->to(),FALSE) + "<br>\n");
    if (!mMsg->cc().isEmpty())
      mViewer->write(i18n("Cc: ")+
                     KMMessage::emailAddrAsAnchor(mMsg->cc(),FALSE) + "<br>\n");
#ifdef KRN
    if (!mMsg->references().isEmpty())
        mViewer->write(i18n("References: ") +
                       KMMessage::refsAsAnchor(mMsg->references()) + "<br>");
#endif
    mViewer->write("<br>");
    break;

  case HdrFancy:
    mViewer->write(QString("<table><tr><td><img src=") +
		   locate("data", "kmail/pics/kdelogo.xpm") +
                   "></td><td hspace=\"50\"><b><font size=\"+2\">");
    mViewer->write(strToHtml(mMsg->subject()) + "</font></b><br>");
    mViewer->write(i18n("From: ")+
                   KMMessage::emailAddrAsAnchor(mMsg->from(),FALSE) + "<br>\n");
    mViewer->write(i18n("To: ")+
                   KMMessage::emailAddrAsAnchor(mMsg->to(),FALSE) + "<br>\n");
    if (!mMsg->cc().isEmpty())
      mViewer->write(i18n("Cc: ")+
                     KMMessage::emailAddrAsAnchor(mMsg->cc(),FALSE) + "<br>\n");
    mViewer->write(i18n("Date: ")+
                   strToHtml(mMsg->dateStr()) + "<br>\n");
#ifdef KRN
    if (!mMsg->references().isEmpty())
        mViewer->write(i18n("References: ") +
                       KMMessage::refsAsAnchor(mMsg->references()) + "<br><br>\n");
#endif
    mViewer->write("</b></td></tr></table><br>\n");
    break;

  case HdrLong:
    mViewer->write("<font size=\"+1\"><b>" +
                   strToHtml(mMsg->subject()) + "</B></font><br>");
    mViewer->write(i18n("Date: ")+strToHtml(mMsg->dateStr())+"<br>");
    mViewer->write(i18n("From: ")+
		   KMMessage::emailAddrAsAnchor(mMsg->from(),FALSE) + "<br>");
    mViewer->write(i18n("To: ")+
                   KMMessage::emailAddrAsAnchor(mMsg->to(),FALSE) + "<br>");
    if (!mMsg->cc().isEmpty())
      mViewer->write(i18n("Cc: ")+
		     KMMessage::emailAddrAsAnchor(mMsg->cc(),FALSE) + "<br>");
    if (!mMsg->bcc().isEmpty())
      mViewer->write(i18n("Bcc: ")+
		     KMMessage::emailAddrAsAnchor(mMsg->bcc(),FALSE) + "<br>");
    if (!mMsg->replyTo().isEmpty())
      mViewer->write(i18n("Reply to: ")+
		     KMMessage::emailAddrAsAnchor(mMsg->replyTo(),FALSE) + "<br>");
#ifdef KRN
    if (!mMsg->references().isEmpty())
        mViewer->write(i18n("References: ")+
                       KMMessage::refsAsAnchor(mMsg->references()) + "<br>\n");
    if (!mMsg->groups().isEmpty())
        mViewer->write(i18n("Groups: ") + mMsg->groups()+"<br>\n");
#endif
    mViewer->write("<br>\n");
    break;

  case HdrAll:
    str = strToHtml(mMsg->headerAsString());
    mViewer->write(str);
    mViewer->write("\n<br>\n");
    break;

  default:
    warning("Unsupported header style %d", mHeaderStyle);
  }
  mViewer->write("<br>\n");
}


//-----------------------------------------------------------------------------
void KMReaderWin::writeBodyStr(const QString aStr)
{
  QString line, sig, htmlStr = "";
  Kpgp* pgp = Kpgp::getKpgp();
  assert(pgp != NULL);
  //  assert(!aStr.isNull());
  bool pgpMessage = false;

  if (pgp->setMessage(aStr))
  {
    QString str = pgp->frontmatter();
    if(!str.isEmpty()) htmlStr += quotedHTML(str);
    htmlStr += "<br>";
    if (pgp->isEncrypted())
    {
      pgpMessage = true;
      if(pgp->decrypt())
      {
	htmlStr += QString("<b>%1</b><br>").arg(i18n("Encrypted message"));
      }
      else
      {
	htmlStr += QString("<b>%1</b><br>%2<br><br>")
                    .arg(i18n("Cannot decrypt message:"))
                    .arg(pgp->lastErrorMsg());
      }
    }
    // check for PGP signing
    if (pgp->isSigned())
    {
      pgpMessage = true;
      if (pgp->goodSignature())
         sig = i18n("Message was signed by");
      else
         sig = i18n("Warning: Bad signature from");

      /* HTMLize signedBy data */
      QString sdata=pgp->signedBy();
      sdata.replace(QRegExp("\""), "&quot;");
      sdata.replace(QRegExp("<"), "&lt;");
      sdata.replace(QRegExp(">"), "&gt;");

      if (sdata.contains(QRegExp("unknown key ID")))
      {
         sdata.replace(QRegExp("unknown key ID"), i18n("unknown key ID"));
         htmlStr += QString("<b>%1 %2</b><br>").arg(sig).arg(sdata);
      }
      else {
         htmlStr += QString("<b>%1 <a href=\"mailto:%2\">%3</a></b><br>")
                      .arg(sig).arg(sdata).arg(sdata);
      }
    }
    htmlStr += quotedHTML(pgp->message());
    if(pgpMessage) htmlStr += "<br><b>End pgp message</b><br><br>";
    str = pgp->backmatter();
    if(!str.isEmpty()) htmlStr += quotedHTML(str);
  }
  else htmlStr += quotedHTML(aStr);

  mViewer->write(htmlStr);
}




//-----------------------------------------------------------------------------

QString KMReaderWin::quotedHTML(const QString& s)
{
  QString htmlStr, line, tmpStr;
  QChar ch;

  bool atStartOfLine;
  int pos, beg;

  int currQuoteLevel = -1;
  int prevQuoteLevel = -1;
  int newlineCount = 0;
  tmpStr = "<div>"; //work around KHTML slowness

  // skip leading empty lines
  for( pos = 0; pos < (int)s.length() && s[pos] <= ' '; pos++ );
  beg = pos;

  atStartOfLine = TRUE;
  while( pos < (int)s.length() )
  {
    ch = s[pos];
    if( ch == '\n' )
    {
      newlineCount ++;
      line = strToHtml(s.mid(beg,pos-beg),TRUE,TRUE);
      if( currQuoteLevel >= 0 )
      {
	if( currQuoteLevel != prevQuoteLevel )
	{
	  line.prepend( mQuoteFontTag[currQuoteLevel%3] );
	  if( prevQuoteLevel >= 0 )
	  {
	    line.prepend( "</font>" );
	  }
	}
	prevQuoteLevel = currQuoteLevel;
      }
      else if( prevQuoteLevel >= 0 )
      {
	line.prepend( "</font><br>\n" ); // Added extra BR to work around bug
	prevQuoteLevel = -1;
      }

      tmpStr += line + "<br>\n";
      if( (newlineCount % 100) == 0 ) 
      {
	htmlStr += tmpStr;
	htmlStr += "</div><div>"; //work around KHTML slowness
	tmpStr.truncate(0);
      }

      beg = pos + 1;
      atStartOfLine = TRUE;
      currQuoteLevel = -1;

    }
    else if( ch > ' ' )
    {
      if( atStartOfLine == TRUE && (ch=='>' || /*ch==':' ||*/ ch=='|') )
      {
	if( mRecyleQouteColors == true || currQuoteLevel < 2 )
	{
	  currQuoteLevel += 1;
	}
      } 
      else
      {
	atStartOfLine = FALSE;
      }
    }
    
    pos++;
  }

  htmlStr += tmpStr;
  htmlStr += "</div>"; //work around KHTML slowness
  return htmlStr;
}



//-----------------------------------------------------------------------------
void KMReaderWin::writePartIcon(KMMessagePart* aMsgPart, int aPartNum)
{
  QString iconName, href, label, comment, contDisp;
  QString fileName;

  if(aMsgPart == NULL) {
    debug("writePartIcon: aMsgPart == NULL\n");
    return;
  }

  debug("writePartIcon: PartNum: %i",aPartNum);

  comment = aMsgPart->contentDescription();

  fileName = aMsgPart->fileName();
  if (fileName.isEmpty()) fileName = aMsgPart->name();
  label = fileName;

//--- Sven's save attachments to /tmp start ---
  bool ok = true;

  QString fname = QString("%1/part%2").arg(mAttachDir).arg(aPartNum+1);
  if (access(fname.data(), W_OK) != 0) // Not there or not writable
    if (mkdir(fname.data(), 0) != 0 || chmod (fname.data(), S_IRWXU) != 0)
      ok = false; //failed create

  if (ok)
  {
    if (fileName.isEmpty())
      fname += "/unnamed";
    else
    {
      fname = fname + "/" + fileName;
      // remove quotes from the filename so that the shell does not get confused
      int c = 0;
      while ((c = fname.find('"', c)) >= 0)
	fname.remove(c, 1);

      c = 0;
      while ((c = fname.find('\'', c)) >= 0)
	fname.remove(c, 1);
    }

    if (!kByteArrayToFile(aMsgPart->bodyDecoded(), fname, false, false, false))
      ok = false;
  }
  if (ok)
  {
    href = QString("file:")+fname;
    //debug ("Wrote attachment to %s", href.data());
  }
  else {
    //--- Sven's save attachments to /tmp end ---
    href = QString("part://%1").arg(aPartNum+1);
  }

  // sven: for viewing images inline
  if (inlineImage)
    iconName = href;
  else
    iconName = aMsgPart->iconName();
  if (iconName.left(11)=="unknown")
  {
    aMsgPart->magicSetType();
    iconName = aMsgPart->iconName();
  }
  mViewer->write("<table><tr><td><a href=\"" + href + "\"><img src=\"" +
		 iconName + "\" border=\"0\">" + label +
		 "</a></td></tr></table>" + comment + "<br>");
}


//-----------------------------------------------------------------------------
const QString KMReaderWin::strToHtml(const QString aStr, bool aDecodeQP,
				     bool aPreserveBlanks) const
{
  QString qpstr, iStr, result;
  const char *pos;
  char ch, str[256];
  int i, i1, x, len;
  int maxLen = 30000;
  char htmlStr[maxLen+256];
  char* htmlPos;
  bool startOfLine = true;

  if (aDecodeQP) qpstr = KMMsgBase::decodeRFC1522String(aStr);
  else qpstr = aStr;

#define HTML_ADD(str,len) strcpy(htmlPos,str),htmlPos+=len

  htmlPos = htmlStr;
  for (pos=qpstr.data(),x=0; *pos; pos++,x++)
  {
    if ((int)(htmlPos-htmlStr) >= maxLen)
    {
      *htmlPos = '\0';
      result += htmlStr;
      htmlPos = htmlStr;
    }

    ch = *pos;
    if (aPreserveBlanks)
    {
      if (ch==' ')
      {
	if (startOfLine) {
	  HTML_ADD("&nbsp;", 6);
	  pos++, x++;
	  startOfLine = false;
	}
        while (*pos==' ')
        {
	  HTML_ADD(" ", 1);
          pos++, x++;
	  if (*pos==' ') {
	    HTML_ADD("&nbsp;", 6);
	    pos++, x++;
	  }
        }
        pos--, x--;

        continue;
      }
      else if (ch=='\t')
      {
	do
	{
	  HTML_ADD("&nbsp;", 6);
	  x++;
	}
	while((x&7) != 0);
      }
    }
    if (ch=='<') HTML_ADD("&lt;", 4);
    else if (ch=='>') HTML_ADD("&gt;", 4);
    else if (ch=='\n') {
      HTML_ADD("<br>", 4);
      startOfLine = true;
    }
    else if (ch=='&') HTML_ADD("&amp;", 5);
    else if ((ch=='h' && strncmp(pos,"http:", 5)==0) ||
	     (ch=='f' && strncmp(pos,"ftp:", 4)==0) ||
	     (ch=='m' && strncmp(pos,"mailto:", 7)==0))
    {
      for (i=0; *pos && *pos>' ' && i<255; i++, pos++)
	str[i] = *pos;
      pos--;
      while (i>0 && ispunct(str[i-1]) && str[i-1]!='/')
      {
	i--;
	pos--;
      }
      str[i] = '\0';
      HTML_ADD("<a href=\"", 9);
      HTML_ADD(str, strlen(str));
      HTML_ADD("\">", 2);
      HTML_ADD(str, strlen(str));
      HTML_ADD("</a>", 4);
    }
    else if (ch=='@')
    {
      const char *startofstring = qpstr.data();
      const char *startpos = pos;
      for (i=0; pos >= startofstring && *pos
	     && (isalnum(*pos)
		 || *pos=='@' || *pos=='.' || *pos=='_'||*pos=='-'
		 || *pos=='*' || *pos=='[' || *pos==']')
	     && i<255; i++, pos--)
	{
	}
      i1 = i;
      pos++;
      for (i=0; *pos && (isalnum(*pos)||*pos=='@'||*pos=='.'||
			 *pos=='_'||*pos=='-' || *pos=='*'  || *pos=='[' || *pos==']')
	     && i<255; i++, pos++)
      {
	iStr += *pos;
      }
      pos--;
      len = iStr.length();
      while (len>2 && ispunct(*pos) && (pos > startpos))
      {
	len--;
	pos--;
      }
      iStr.truncate(len);

      htmlPos -= (i1 - 1);
      if (iStr.length()>3)
	iStr = "<a href=\"mailto:" + iStr + "\">" + iStr + "</a>";
      HTML_ADD(iStr.data(), iStr.length());
      iStr = "";
    }

    else *htmlPos++ = ch;
  }

  *htmlPos = '\0';
  result += htmlStr;
  return result;
}


//-----------------------------------------------------------------------------
void KMReaderWin::printMsg(void)
{
  if (!mMsg) return;
  // FIXME
  //mViewer->print();
}


//-----------------------------------------------------------------------------
int KMReaderWin::msgPartFromUrl(const char* aUrl)
{
  if (!aUrl || !mMsg) return -1;

  QString url = QString("file:%1/part").arg(mAttachDir);
  int s = url.length();
  if (strncmp(aUrl, url, s) == 0)
  {
    url = aUrl;
    int i = url.find('/', s);
    url = url.mid(s, i-s);
    //debug ("Url num = %s", url.data());
    return atoi(url.data());
  }
  return -1;
}


static bool doDelayedResize=false;

//-----------------------------------------------------------------------------
void KMReaderWin::resizeEvent(QResizeEvent *)
{
  #warning Espen 2000-05-07. Using a delayed resize. Use direct resize
  #warning from time to time to see how the html widget behaves.

  static QTimer *timer = 0;
  if( timer == 0 )
  {
    timer = new QTimer( this );
    connect( timer, SIGNAL(timeout()), SLOT(slotDelayedResize()) );
  }
  if( timer->isActive() )
  {
    //
    // Combine all resize operations that are requested as long a 
    // the timer runs.
    //
    //puts("delayed");
    doDelayedResize = true;
    return;
  }
  
  //
  // Resize now, and start the timer which will cause any new resize
  // operations to be igored as long as the timer is active
  //
  mViewer->widget()->setGeometry(0, 0, width(), height());
  doDelayedResize = false;
  timer->start( 150, true );

  //
  // Orig
  //
  //mViewer->widget()->setGeometry(0, 0, width(), height());
}


void KMReaderWin::slotDelayedResize()
{
  if( doDelayedResize )
  {
    mViewer->widget()->setGeometry(0, 0, width(), height());
    doDelayedResize = false;
  }
  //puts("DONE");
}





//-----------------------------------------------------------------------------
void KMReaderWin::closeEvent(QCloseEvent *e)
{
  KMReaderWinInherited::closeEvent(e);
  writeConfig();
}


//-----------------------------------------------------------------------------
void KMReaderWin::slotUrlOn(const QString &aUrl)
{
  int id;
  KMMessagePart msgPart;
  QString str;
  QString url = aUrl;
  KURL::decode( url );

  id = msgPartFromUrl(url);
  if (id <= 0)
  {
    emit statusMsg(url);
  }
  else
  {
    mMsg->bodyPart(id-1, &msgPart);
    str = msgPart.fileName();
    if (str.isEmpty()) str = msgPart.name();
    emit statusMsg(i18n("Attachment: ") + str);
  }
}


//-----------------------------------------------------------------------------
void KMReaderWin::slotUrlOpen(const KURL &aUrl, const KParts::URLArgs &)
{
  int id;
  QString url = aUrl.decodedURL();

  id = msgPartFromUrl(url);
  if (id > 0)
  {
    // clicked onto an attachment
    mAtmCurrent = id-1;

    slotAtmOpen();
  }
  else emit urlClicked(url,/* aButton*/LeftButton); //### FIXME: add button to URLArgs!
}


//-----------------------------------------------------------------------------
void KMReaderWin::slotUrlPopup(const QString &aUrl, const QPoint& aPos)
{
  KMMessagePart msgPart;
  int id;
  QPopupMenu *menu;
  QString url = aUrl;
  KURL::decode( url );

  id = msgPartFromUrl(url);
  if (id <= 0) emit popupMenu(aUrl, aPos);
  else
  {
    // Attachment popup
    mAtmCurrent = id-1;
    menu = new QPopupMenu();
    menu->insertItem(i18n("Open..."), this, SLOT(slotAtmOpen()));
    menu->insertItem(i18n("View..."), this, SLOT(slotAtmView()));
    menu->insertItem(i18n("Save as..."), this, SLOT(slotAtmSave()));
    //menu->insertItem(i18n("Print..."), this, SLOT(slotAtmPrint()));
    menu->insertItem(i18n("Properties..."), this,
		     SLOT(slotAtmProperties()));
    menu->popup(aPos,0);
  }
}


//-----------------------------------------------------------------------------
void KMReaderWin::atmViewMsg(KMMessagePart* aMsgPart)
{
  KMMessage* msg = new KMMessage;
  assert(aMsgPart!=NULL);

  msg->fromString(aMsgPart->bodyDecoded());
  emit showAtmMsg(msg);
}


//-----------------------------------------------------------------------------
void KMReaderWin::slotAtmView()
{
  QString str, pname;
  KMMessagePart msgPart;
  // ---Sven's view text, html and image attachments in html widget start ---
  // Sven commented out
  //QMultiLineEdit* edt = new QMultiLineEdit;
  // ---Sven's view text, html and image attachments in html widget end ---

  mMsg->bodyPart(mAtmCurrent, &msgPart);
  pname = msgPart.fileName();
  if (pname.isEmpty()) pname=msgPart.name();
  if (pname.isEmpty()) pname=msgPart.contentDescription();
  if (pname.isEmpty()) pname="unnamed";

  if (stricmp(msgPart.typeStr(), "message")==0)
  {
    atmViewMsg(&msgPart);
    return;
  }

  kernel->kbp()->busy();
  // ---Sven's view text, html and image attachments in html widget start ---
  // ***start commenting out old stuff
  //  str = QCString(msgPart.bodyDecoded());

  //edt->setCaption(i18n("View Attachment: ") + pname);
  //edt->insertLine(str);
  //edt->setReadOnly(TRUE);
  //edt->show();
  // *** end commenting out old stuff
  {

    KMReaderWin* win = new KMReaderWin; //new reader

    if (stricmp(msgPart.typeStr(), "text")==0)
    {
      win->mViewer->begin( KURL( "file:/" ) );
      win->mViewer->write("<html><body>");
      QString str = msgPart.bodyDecoded();
      if (htmlMail() && (stricmp(msgPart.subtypeStr(), "html")==0))
        win->mViewer->write(str);
      else  //plain text
        win->writeBodyStr(str);
      win->mViewer->write("</body></html>");
      win->mViewer->end();
      win->setCaption(i18n("View Attachment: ") + pname);
      win->show();
    }
    else if (stricmp(msgPart.typeStr(), "image")==0)
    {
      //image
      // Attachment is saved already; this is the file:
      QString linkName = QString("<img src=\"file:%1/part%2/%3\" border=0>")
                        .arg(mAttachDir).arg(mAtmCurrent+1).arg(pname);
      win->mViewer->begin( KURL( "file:/" ) );
      win->mViewer->write("<html><body>");
      win->mViewer->write(linkName.data());
      win->mViewer->write("</body></html>");
      win->mViewer->end();
      win->setCaption(i18n("View Attachment: ") + pname);
      win->show();
    }
  }
  // ---Sven's view text, html and image attachments in html widget end ---
  kernel->kbp()->idle();
}


//-----------------------------------------------------------------------------
void KMReaderWin::slotAtmOpen()
{
  QString str, pname, cmd, fileName;
  KMMessagePart msgPart;
  // char* tmpName;
  // int old_umask;
  int c;

  mMsg->bodyPart(mAtmCurrent, &msgPart);

  if (stricmp(msgPart.typeStr(), "message")==0)
  {
    atmViewMsg(&msgPart);
    return;
  }

  pname = msgPart.fileName();
  if (pname.isEmpty()) pname=msgPart.name();
  if (pname.isEmpty()) pname="unnamed";
  //--- Sven's save attachments to /tmp start ---
  // Sven added:
  fileName = QString("%1/part%2/%3")
             .arg(mAttachDir).arg(mAtmCurrent+1).arg(pname);
  // Sven commented out:
  //tmpName = tempnam(NULL, NULL);
  //if (!tmpName)
  //{
  //  warning(i18n("Could not create temporary file"));
  //  return;
  //}
  //fileName = tmpName;
  //free(tmpName);
  //fileName += '-';
  //fileName += pname;

  // remove quotes from the filename so that the shell does not get confused
  c = 0;
  while ((c = fileName.find('"', c)) >= 0)
    fileName.remove(c, 1);

  c = 0;
  while ((c = fileName.find('\'', c)) >= 0)
    fileName.remove(c, 1);

  // Sven commented out:
  //kernel->kbp()->busy();
  // NOTE: this next line will not work with Qt 2.0 - use a QByteArray str.
  //str = msgPart.bodyDecoded();
  //old_umask = umask(077);
  //if (!kCStringToFile(str, fileName, TRUE))
  //  warning(i18n("Could not save temporary file %s"),
  //	    (const char*)fileName);
  //umask(old_umask);
  //kernel->kbp()->idle();
  //--- Sven's save attachments to /tmp end ---

  // -- David : replacement for KFM::openURL
  if ( !KOpenWithHandler::exists() )
    (void) new KFileOpenWithHandler();
  (void) new KRun(fileName);
}


//-----------------------------------------------------------------------------
void KMReaderWin::slotAtmSave()
{
  KMMessagePart msgPart;
  QString fileName;
  fileName = QDir::currentDirPath();
  fileName.append("/");


  mMsg->bodyPart(mAtmCurrent, &msgPart);
  fileName.append(msgPart.name());

  KURL url = KFileDialog::getSaveURL( fileName, "*", this );

  if( url.isEmpty() )
    return;

  if( !url.isLocalFile() )
  {
    KMessageBox::sorry( 0L, i18n( "Only local files supported yet." ) );
    return;
  }

  fileName = url.path();

  kernel->kbp()->busy();
  if (!kByteArrayToFile(msgPart.bodyDecoded(), fileName, TRUE))
    warning(i18n("Could not save file"));
  kernel->kbp()->idle();
}


//-----------------------------------------------------------------------------
void KMReaderWin::slotAtmPrint()
{
  KMMessagePart msgPart;
  mMsg->bodyPart(mAtmCurrent, &msgPart);

  warning("KMReaderWin::slotAtmPrint()\nis not implemented");
}


//-----------------------------------------------------------------------------
void KMReaderWin::slotAtmProperties()
{
  KMMessagePart msgPart;
  KMMsgPartDlg  dlg(0,TRUE);

  kernel->kbp()->busy();
  mMsg->bodyPart(mAtmCurrent, &msgPart);
  dlg.setMsgPart(&msgPart);
  kernel->kbp()->idle();

  dlg.exec();
}

//-----------------------------------------------------------------------------
void KMReaderWin::slotScrollUp()
{
  static_cast<QScrollView *>(mViewer->widget())->scrollBy(0, -10);	
}


//-----------------------------------------------------------------------------
void KMReaderWin::slotScrollDown()
{
  static_cast<QScrollView *>(mViewer->widget())->scrollBy(0, 10);	
}


//-----------------------------------------------------------------------------
void KMReaderWin::slotScrollPrior()
{
  static_cast<QScrollView *>(mViewer->widget())->scrollBy(0, -(int)(height()*0.8));	
}


//-----------------------------------------------------------------------------
void KMReaderWin::slotScrollNext()
{
  static_cast<QScrollView *>(mViewer->widget())->scrollBy(0, (int)(height()*0.8));	
}


//-----------------------------------------------------------------------------
void KMReaderWin::slotDocumentChanged()
{

}


//-----------------------------------------------------------------------------
void KMReaderWin::slotTextSelected(bool)
{

/*  ### FIXME
  QString temp = mViewer->selectedText();
  kapp->clipboard()->setText(temp);
*/
}


//-----------------------------------------------------------------------------
QString KMReaderWin::copyText()
{
/* ### FIXME
  QString temp = mViewer->selectedText();
  return temp;
*/
  return QString();
}


//-----------------------------------------------------------------------------
void KMReaderWin::slotDocumentDone()
{
  // mSbVert->setValue(0);
}


//-----------------------------------------------------------------------------
void KMReaderWin::setHtmlOverride(bool override)
{
  mHtmlOverride = override;
}


//-----------------------------------------------------------------------------
bool KMReaderWin::htmlMail()
{
  return ((mHtmlMail && !mHtmlOverride) || (!mHtmlMail && mHtmlOverride));
}

//-----------------------------------------------------------------------------
#include "kmreaderwin.moc"
