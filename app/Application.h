// The QApplication subclass: application lifetime, and the translator stack.
//
// ---------------------------------------------------------------------------------------------
// WHY TWO TRANSLATORS ARE INSTALLED AND NOT ONE.
//
// The catalog key IS the source string in this project (T6): `core/Result.h` forbids user-facing
// English in `core/`, `ActionId.def` carries one `action.*` key per action, and so there is no
// English in the sources for `lupdate` to extract. Every locale, `en` included, is a real catalog.
//
// The consequence is that an UNTRANSLATED string does not fall back to English -- it falls back to
// the key, and a button reads `action.file.open`. So `rawpage_en.qm` is installed first as a base
// and the active locale on top of it: Qt tries translators in reverse order of installation, so
// zh-TW answers where it has a key and `en` answers where it does not. A key added to `en` and not
// yet to a shipped locale therefore renders as English -- the difference between an incomplete
// translation and a broken UI.
//
// (Measured at T9 rather than assumed: the 28 zh-TW entries marked `unfinished` are NOT the case
// this covers. lrelease keeps an unfinished message that has text, so those reach the product as
// provisional Chinese and are exactly what M-06 reviews. Only an EMPTY translation is dropped.)
//
// A key missing from `en` remains a detectable DEFECT rather than an invisible fallback: it renders
// as the key, and `tools/i18n-lint`'s `missing-key` rule fails the build before anyone sees it.
// ---------------------------------------------------------------------------------------------
//
// Stage 4 adds session restore here; `general/restoreSession` is declared and read by nothing yet,
// which is stated rather than quietly implemented, because `SessionState` is that stage's file.

#ifndef RAWPAGE_APP_APPLICATION_H
#define RAWPAGE_APP_APPLICATION_H

#include <QApplication>
#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

class QTranslator;

namespace rawpage::app {

class Application : public QApplication {
    Q_OBJECT

public:
    Application(int& argc, char** argv);
    ~Application() override;

    // Resolution order, highest first: `--lang`, then `language/uiLocale`, then the system locale.
    // Returns the locale actually in force, which may be `en` if the requested catalog is one of
    // the two declared `"shipped": false` in i18n/locales.json (D1-14).
    QString loadTranslators(const QString& requestedLocale, const QString& settingLocale);

    const QString& activeLocale() const { return activeLocale_; }

    // Which catalog files were installed, most specific last. Printed by --dump-ui-strings so that
    // "the translation did not load" and "the translation loaded and is incomplete" are
    // distinguishable without a debugger.
    const QStringList& loadedCatalogs() const { return loadedCatalogs_; }

    // Where the .qm files live: `<application dir>/i18n`. A directory beside the executable rather
    // than a Qt resource, because Stage 15 ships them as replaceable files -- a customer-supplied
    // catalog is a real deployment request and a resource would make it a rebuild.
    static QString catalogDirectory();

private:
    bool installCatalog(const QString& localeCode);

    std::vector<std::unique_ptr<QTranslator>> translators_;
    QString     activeLocale_;
    QStringList loadedCatalogs_;
};

} // namespace rawpage::app

#endif // RAWPAGE_APP_APPLICATION_H
