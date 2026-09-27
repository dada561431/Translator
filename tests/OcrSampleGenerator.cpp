#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QTextStream>

namespace {

struct Sample
{
    QString fileName;
    QString text;
    QString fontFamily;
    int pointSize;
    QSize size;
    QColor foreground;
    QColor background;
    bool outline = false;
};

bool renderSample(const QDir &directory, const Sample &sample)
{
    QImage image(sample.size, QImage::Format_RGB32);
    image.fill(sample.background);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    QFont font(sample.fontFamily, sample.pointSize);
    font.setStyleStrategy(QFont::PreferAntialias);
    painter.setFont(font);

    if (sample.outline) {
        QPainterPath path;
        const QFontMetrics metrics(font);
        const QPointF origin((image.width() - metrics.horizontalAdvance(sample.text)) / 2.0,
                             (image.height() + metrics.ascent() - metrics.descent()) / 2.0);
        path.addText(origin, font, sample.text);
        painter.setPen(QPen(Qt::black, 4.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(sample.foreground);
        painter.drawPath(path);
    } else {
        painter.setPen(sample.foreground);
        painter.drawText(image.rect(), Qt::AlignCenter, sample.text);
    }

    return image.save(directory.filePath(sample.fileName), "PNG");
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication application(argc, argv);
    QTextStream output(stdout);
    if (application.arguments().size() != 2) {
        output << "usage: TranslatorOcrSampleGenerator <output-directory>\n";
        return 2;
    }

    QDir directory(application.arguments().at(1));
    if (!directory.mkpath(QStringLiteral("."))) {
        output << "Could not create output directory: " << directory.absolutePath() << '\n';
        return 2;
    }

    const QList<Sample> samples = {
        {QStringLiteral("english-hello.png"), QStringLiteral("Hello Phase 4"),
         QStringLiteral("Segoe UI"), 32, QSize(520, 80), Qt::black, Qt::white},
        {QStringLiteral("english-qt.png"), QStringLiteral("Qt Screen OCR Test"),
         QStringLiteral("Segoe UI"), 28, QSize(520, 80), Qt::white, Qt::black},
        {QStringLiteral("english-codes.png"), QStringLiteral("ABCDEFG 987654"),
         QStringLiteral("Consolas"), 28, QSize(520, 70), Qt::black, Qt::white},
        {QStringLiteral("zh-yahei.png"), QStringLiteral("你好世界"),
         QStringLiteral("Microsoft YaHei"), 32, QSize(520, 90), Qt::black, Qt::white},
        {QStringLiteral("zh-simsun.png"), QStringLiteral("你好世界"),
         QStringLiteral("SimSun"), 32, QSize(520, 90), Qt::black, Qt::white},
        {QStringLiteral("zh-simhei.png"), QStringLiteral("你好世界"),
         QStringLiteral("SimHei"), 32, QSize(520, 90), Qt::black, Qt::white},
        {QStringLiteral("zh-kaiti.png"), QStringLiteral("你好世界"),
         QStringLiteral("KaiTi"), 32, QSize(520, 90), Qt::black, Qt::white},
        {QStringLiteral("zh-test.png"), QStringLiteral("这是中文OCR测试"),
         QStringLiteral("Microsoft YaHei"), 28, QSize(620, 90), Qt::white, Qt::black},
        {QStringLiteral("zh-translation.png"), QStringLiteral("实时屏幕翻译"),
         QStringLiteral("Microsoft YaHei"), 28, QSize(620, 90), Qt::black, Qt::white},
        {QStringLiteral("zh-small.png"), QStringLiteral("实时屏幕翻译"),
         QStringLiteral("Microsoft YaHei"), 15, QSize(300, 42), Qt::black, Qt::white},
        {QStringLiteral("zh-outline.png"), QStringLiteral("白字黑描边字幕"),
         QStringLiteral("Microsoft YaHei"), 28, QSize(620, 90), Qt::white, QColor(62, 94, 122), true},
        {QStringLiteral("blank.png"), QString(), QStringLiteral("Segoe UI"),
         28, QSize(520, 90), Qt::black, Qt::white},
    };

    for (const Sample &sample : samples) {
        if (!renderSample(directory, sample)) {
            output << "Could not save sample: " << sample.fileName << '\n';
            return 1;
        }
        output << sample.fileName << '\t' << sample.size.width() << 'x'
               << sample.size.height() << '\t' << sample.fontFamily << '\t'
               << sample.text << '\n';
    }
    return 0;
}
