#include "niristate.h"
#include "niriselectors.h"
#include "nirisession.h"
#include "generated/niriipccodec_generated.h"
#include <QCoreApplication>
#include <QPointer>

namespace {
template <typename T>
std::optional<T> copy(NiriEventStreamState::Entry<T> entry)
{
    return entry ? std::optional<T>(*entry) : std::nullopt;
}
template <typename T>
bool countChanged(const QMap<quint64, NiriEventStreamState::RecordChange<T>> &changes)
{
    int delta = 0;
    for (const auto &c : changes)
        delta += int(bool(c.after)) - int(bool(c.before));
    return delta != 0;
}
template <typename T>
QVariantMap keyed(const QList<T> &records, quint64 T::*id)
{
    QVariantMap map;
    for (const auto &record : records)
        map.insert(QString::number(record.*id), QVariant::fromValue(record));
    return map;
}
std::optional<quint64> inputId(const QVariant &input)
{
    std::optional<quint64> id;
    NiriIpc::DecodeError error;
    if (!NiriIpc::idInput(input, id, error))
        return std::nullopt;
    return id;
}
} // namespace

NiriState *NiriState::create(QQmlEngine *engine, QJSEngine *jsEngine)
{
    if (!NiriSession::checkThread(engine ? engine : jsEngine))
        return nullptr;
    QObject *owner = engine ? static_cast<QObject *>(engine) : static_cast<QObject *>(jsEngine);
    if (!owner)
        return instance();
    auto *self = new NiriState(owner);
    QJSEngine::setObjectOwnership(self, QJSEngine::CppOwnership);
    NiriSession::instance()->ensureStarted();
    return self;
}
NiriState *NiriState::instance()
{
    if (!NiriSession::checkThread())
        return nullptr;
    auto *app = QCoreApplication::instance();
    auto *self = app->findChild<NiriState *>(QStringLiteral("niriqml.state"),
                                             Qt::FindDirectChildrenOnly);
    if (!self) {
        self = new NiriState(app);
        self->setObjectName(QStringLiteral("niriqml.state"));
    }
    NiriSession::instance()->ensureStarted();
    return self;
}
NiriState::NiriState(QObject *parent) : QObject(parent)
{
    auto *session = NiriSession::instance();
    m_revision = session->state().revision();
    connect(session, &NiriSession::stateUpdated, this, &NiriState::updated);
    session->ensureStarted();
}
const NiriEventStreamState &NiriState::state() const
{
    if (NiriSession::checkThread(this))
        return NiriSession::instance()->state();
    static const NiriEventStreamState empty;
    return empty;
}
std::optional<quint64> NiriState::focusedWindowId() const
{
    return state().focusedWindowId();
}
std::optional<quint64> NiriState::focusedWorkspaceId() const
{
    return state().focusedWorkspaceId();
}
std::optional<NiriWindow> NiriState::focusedWindow() const
{
    return copy(NiriSelectors::window(state(), focusedWindowId()));
}
std::optional<NiriWorkspace> NiriState::focusedWorkspace() const
{
    return copy(NiriSelectors::workspace(state(), {}, focusedWorkspaceId()));
}
std::optional<QString> NiriState::focusedWindowAppId() const
{
    const auto w = focusedWindow();
    return w ? w->appId : std::nullopt;
}
std::optional<QString> NiriState::focusedWindowTitle() const
{
    const auto w = focusedWindow();
    return w ? w->title : std::nullopt;
}
std::optional<QString> NiriState::activeOutput() const
{
    const auto w = focusedWorkspace();
    return w ? w->output : std::nullopt;
}
int NiriState::windowCount() const
{
    return state().windowCount();
}
int NiriState::workspaceCount() const
{
    return state().workspaceCount();
}
int NiriState::castCount() const
{
    return state().castCount();
}
bool NiriState::initialStateReceived() const
{
    return state().initialStateReceived();
}
std::optional<NiriKeyboardLayouts> NiriState::keyboardLayouts() const
{
    return state().keyboardLayouts();
}
NiriOverview NiriState::overview() const
{
    NiriOverview value;
    value.isOpen = overviewOpen();
    return value;
}
bool NiriState::overviewOpen() const
{
    return state().overviewOpen();
}
bool NiriState::configFailed() const
{
    return state().configFailed();
}
std::optional<bool> NiriState::touchpadOn() const
{
    return state().touchpadOn();
}
std::optional<NiriWindow> NiriState::nativeWindow(quint64 id) const
{
    return copy(state().window(id));
}
std::optional<NiriWorkspace> NiriState::nativeWorkspace(quint64 id) const
{
    return copy(state().workspace(id));
}
std::optional<NiriWorkspace> NiriState::nativeWorkspaceByName(const QString &name) const
{
    return copy(state().workspaceByName(name));
}
std::optional<NiriCast> NiriState::nativeCast(quint64 id) const
{
    return copy(state().cast(id));
}
QList<NiriWindow> NiriState::nativeWindowsSnapshot() const
{
    return state().windowsSnapshot();
}
QList<NiriWorkspace> NiriState::nativeWorkspacesSnapshot() const
{
    return state().workspacesSnapshot();
}
QList<NiriCast> NiriState::nativeCastsSnapshot() const
{
    return state().castsSnapshot();
}
QVariant NiriState::window(const QVariant &input) const
{
    const auto id = inputId(input);
    return id ? NiriIpc::qmlValue(nativeWindow(*id)) : QVariant::fromValue(nullptr);
}
QVariant NiriState::workspace(const QVariant &input) const
{
    const auto id = inputId(input);
    return id ? NiriIpc::qmlValue(nativeWorkspace(*id)) : QVariant::fromValue(nullptr);
}
QVariant NiriState::workspaceByName(const QString &name) const
{
    return NiriIpc::qmlValue(nativeWorkspaceByName(name));
}
QVariant NiriState::cast(const QVariant &input) const
{
    const auto id = inputId(input);
    return id ? NiriIpc::qmlValue(nativeCast(*id)) : QVariant::fromValue(nullptr);
}
QVariantList NiriState::windowsSnapshot() const
{
    return NiriIpc::qmlValue(nativeWindowsSnapshot()).toList();
}
QVariantList NiriState::workspacesSnapshot() const
{
    return NiriIpc::qmlValue(nativeWorkspacesSnapshot()).toList();
}
QVariantList NiriState::castsSnapshot() const
{
    return NiriIpc::qmlValue(nativeCastsSnapshot()).toList();
}
QVariantMap NiriState::windowsById() const
{
    return keyed(nativeWindowsSnapshot(), &NiriWindow::id);
}
QVariantMap NiriState::workspacesById() const
{
    return keyed(nativeWorkspacesSnapshot(), &NiriWorkspace::id);
}
QVariantMap NiriState::castsByStreamId() const
{
    return keyed(nativeCastsSnapshot(), &NiriCast::streamId);
}
QVariantMap NiriState::snapshot() const
{
    return {{"windowsById", windowsById()},
            {"workspacesById", workspacesById()},
            {"castsByStreamId", castsByStreamId()},
            {"keyboardLayouts", qmlKeyboardLayouts()},
            {"overview", QVariant::fromValue(overview())},
            {"overviewOpen", overviewOpen()},
            {"configFailed", configFailed()},
            {"touchpadOn", qmlTouchpadOn()},
            {"initialStateReceived", initialStateReceived()},
            {"focusedWindowId", qmlFocusedWindowId()},
            {"focusedWorkspaceId", qmlFocusedWorkspaceId()},
            {"activeOutput", qmlActiveOutput()}};
}

void NiriState::updated(const NiriEventStreamState::Changes &c)
{
    if (c.revision <= m_revision)
        return;
    m_revision = c.revision;
    if (!c.changed())
        return;
    const auto oldWindow = copy(NiriSelectors::window(state(), c.focusedWindowBefore, &c));
    const auto oldWorkspace =
            copy(NiriSelectors::workspace(state(), {}, c.focusedWorkspaceBefore, &c));
    const auto win = focusedWindow();
    const auto ws = focusedWorkspace();
    QPointer<NiriState> guard(this);
#define NOTIFY(condition, signal)                                                \
    if (condition) {                                                             \
        emit signal();                                                           \
        if (!guard || NiriSession::instance()->state().revision() != c.revision) \
            return;                                                              \
    }
    NOTIFY(c.focusedWindowBefore != focusedWindowId(), focusedWindowIdChanged)
    NOTIFY(c.focusedWorkspaceBefore != focusedWorkspaceId(), focusedWorkspaceIdChanged)
    NOTIFY(oldWindow != win, focusedWindowChanged)
    NOTIFY(oldWorkspace != ws, focusedWorkspaceChanged)
    NOTIFY((oldWindow ? oldWindow->title : std::nullopt) != (win ? win->title : std::nullopt),
           focusedWindowTitleChanged)
    NOTIFY((oldWindow ? oldWindow->appId : std::nullopt) != (win ? win->appId : std::nullopt),
           focusedWindowAppIdChanged)
    NOTIFY((oldWorkspace ? oldWorkspace->output : std::nullopt) != (ws ? ws->output : std::nullopt),
           activeOutputChanged)
    NOTIFY(countChanged(c.windows), windowCountChanged)
    NOTIFY(countChanged(c.workspaces), workspaceCountChanged)
    NOTIFY(countChanged(c.casts), castCountChanged)
    NOTIFY(!c.windows.isEmpty(), windowsChanged)
    NOTIFY(!c.workspaces.isEmpty(), workspacesChanged)
    NOTIFY(!c.casts.isEmpty(), castsChanged)
    NOTIFY(c.keyboardLayouts, keyboardLayoutsChanged)
    NOTIFY(c.overview, overviewOpenChanged)
    NOTIFY(c.config, configFailedChanged)
    NOTIFY(c.touchpad, touchpadOnChanged)
    NOTIFY(c.readiness, initialStateReceivedChanged)
    NOTIFY(true, stateChanged)
#undef NOTIFY
}
