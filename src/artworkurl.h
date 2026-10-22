#pragma once
#include <QRegularExpression>
#include <QUrl>

namespace artworkurl {
inline bool isQQMusic(const QUrl &url) {
  const auto host=url.host().toLower();
  return url.scheme()=="https" && url.userInfo().isEmpty() &&
    (host=="qq.com" || host.endsWith(".qq.com") || host=="gtimg.cn" || host.endsWith(".gtimg.cn") || host=="qpic.cn" || host.endsWith(".qpic.cn") || host=="qlogo.cn" || host.endsWith(".qlogo.cn"));
}
inline QUrl forDesktop(const QUrl &url) {
  return url.isLocalFile()?QUrl::fromLocalFile(url.toLocalFile()):url;
}
inline QUrl sized(const QUrl &source,int pixels) {
  if(!isQQMusic(source))return source;
  static const QRegularExpression dimensions("R(\\d+)x(\\d+)M");
  QUrl url=source;auto path=url.path();const auto match=dimensions.match(path);
  if(match.hasMatch()) {
    const int size=qBound(150,qMax(pixels,match.captured(1).toInt()),1600);
    path.replace(match.capturedStart(),match.capturedLength(),QString("R%1x%1M").arg(size));url.setPath(path);
  }
  return url;
}
}
