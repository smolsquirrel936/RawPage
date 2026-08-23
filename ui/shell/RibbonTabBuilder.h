// Spec + registry -> widgets. The only thing that places a command on the ribbon.
//
// There is no widget code anywhere in RawPage that says "put Open here". `res/ribbon.json` says it,
// `ui/shell/RibbonModel` parses it, and this class builds it. That is D1-8's promise in one
// sentence: adding a command in Stage 9 is a registry row plus a JSON line, never a diff to a
// widget -- and the promise only holds while nothing else is allowed to place a button, which is
// why `ActionFactory` is the only constructor of actions and this is its only ribbon caller.
//
// WHAT IT DELIBERATELY DOES NOT DO. It makes no decision the resource could have expressed: not
// which tab a command belongs to, not which group, not how large its button is, not what collapses
// first. Every one of those is a field in the JSON. A builder that "knows" the Home tab is special
// is a builder that has to be edited whenever the ribbon changes, which is the state this design
// exists to avoid.

#ifndef RAWPAGE_UI_SHELL_RIBBONTABBUILDER_H
#define RAWPAGE_UI_SHELL_RIBBONTABBUILDER_H

#include "ui/shell/RibbonModel.h"

#include <QString>
#include <QStringList>

namespace rawpage::core { class ActionRegistry; }

namespace rawpage::ui {

class ActionFactory;
class Ribbon;

class RibbonTabBuilder {
public:
    RibbonTabBuilder(Ribbon& ribbon, ActionFactory& actions);

    // Builds every tab in `spec` into the ribbon, in file order. Returns the problems it could not
    // build past -- empty on a sound spec.
    //
    // A spec that failed to parse still reaches here rather than being refused earlier, and builds
    // whatever WAS sound. The reasoning is the same one that makes an unregistered action a
    // disabled button rather than a missing one: a shell that opens with a thin ribbon and a log
    // line naming the resource is diagnosable, and one that refuses to open is a support call. The
    // build-time validator is what stops that path ever being reached by a shipped build.
    QStringList build(const ribbon::ParseResult& parsed);

private:
    Ribbon&        ribbon_;
    ActionFactory& actions_;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_SHELL_RIBBONTABBUILDER_H
