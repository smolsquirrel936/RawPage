#include "app/Application.h"

#include "core/Log.h"

#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QTranslator>

namespace rawpage::app {
namespace {

using core::LogSubsystem;

constexpr const char* kSourceLocale = "en";

// "zh-TW" and "zh_TW" are the same locale spelled two ways -- BCP-47 uses the hyphen, Qt's file
// naming convention uses the underscore, and a person typing --lang will use whichever they saw
// last. Normalising here means the rest of the file has one spelling to reason about.
QString normalizeLocale(const QString& code)
{
    return QString(code).trimmed().replace(QLatin1Char('-'), QLatin1Char('_'));
}

QString languageOf(const QString& normalized)
{
    const qsizetype underscore = normalized.indexOf(QLatin1Char('_'));
    return underscore < 0 ? normalized : normalized.left(underscore);
}

} // namespace

Application::Application(int& argc, char** argv)
    : QApplication(argc, argv)
{
    // Stage 1 has one window. Stage 4 brings a window set and a "last window closed" policy that
    // has to consider whether any document is still dirty; until then Qt's default -- quit when the
    // last window closes -- is both correct and the one people expect.
    setQuitOnLastWindowClosed(true);
}

Application::~Application() = default;

QString Application::catalogDirectory()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("i18n"));
}

bool Application::installCatalog(const QString& localeCode)
{
    const QString path =
        QDir(catalogDirectory()).filePath(QStringLiteral("rawpage_%1.qm").arg(localeCode));
    if (!QFileInfo::exists(path)) return false;

    auto translator = std::make_unique<QTranslator>();
    if (!translator->load(path)) {
        // A .qm that exists and will not load is a build or deployment defect, not a missing
        // translation, and the two want different fixes -- so they get different log lines.
        RAWPAGE_LOG_WARNING(LogSubsystem::Ui) << "i18n: catalog present but unreadable:" << path;
        return false;
    }

    if (!installTranslator(translator.get())) {
        RAWPAGE_LOG_WARNING(LogSubsystem::Ui) << "i18n: Qt refused the catalog:" << path;
        return false;
    }

    translators_.push_back(std::move(translator));
    loadedCatalogs_ << path;
    return true;
}

QString Application::loadTranslators(const QString& requestedLocale, const QString& settingLocale)
{
    // The base layer. Installed FIRST so that everything installed after it wins where it has a
    // key -- see the header for why this is not the usual Qt arrangement.
    if (!installCatalog(QLatin1String(kSourceLocale))) {
        // Not fatal, and deliberately loud: every string in the product renders as its catalog key
        // from here, which looks like a catastrophic bug and is in fact a missing file.
        RAWPAGE_LOG_ERROR(LogSubsystem::Ui)
            << "i18n: the source catalog rawpage_en.qm is missing from" << catalogDirectory()
            << "-- the UI will render catalog keys";
    }

    QString wanted = normalizeLocale(requestedLocale);
    if (wanted.isEmpty()) wanted = normalizeLocale(settingLocale);
    if (wanted.isEmpty()) wanted = normalizeLocale(QLocale::system().name());

    activeLocale_ = QLatin1String(kSourceLocale);

    if (languageOf(wanted) == QLatin1String(kSourceLocale)) {
        // Already installed as the base; installing it twice would work and would make
        // loadedCatalogs() misleading.
        return activeLocale_;
    }

    // Exact first, then language-only: `zh_TW` and `zh` are different catalogs by decision D1-14
    // ("not a Traditional-to-Simplified conversion: the terminology differs"), so a request for
    // `zh_TW` must never be satisfied by a `zh_CN` catalog -- but a request for `pt_BR` against a
    // shipped `pt` legitimately is.
    if (installCatalog(wanted)) {
        activeLocale_ = wanted;
    } else if (const QString language = languageOf(wanted);
               language != wanted && installCatalog(language)) {
        activeLocale_ = language;
    } else {
        // The declared-but-unshipped locales (zh_CN, ja) land here, and so does a typo. Both are
        // reported the same way because from the running process they are the same fact: there is
        // no catalog for what was asked for.
        RAWPAGE_LOG_WARNING(LogSubsystem::Ui)
            << "i18n: no catalog for" << wanted << "in" << catalogDirectory()
            << "-- falling back to" << QLatin1String(kSourceLocale);
    }

    RAWPAGE_LOG_INFO(LogSubsystem::Ui)
        << "i18n: locale" << activeLocale_ << "--" << static_cast<int>(loadedCatalogs_.size())
        << "catalog(s) installed";
    return activeLocale_;
}

} // namespace rawpage::app
