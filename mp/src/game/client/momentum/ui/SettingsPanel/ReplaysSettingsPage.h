#pragma once

#include "cbase.h"

#include "SettingsPage.h"
#include "CVarSlider.h"
#include <vgui_controls/Button.h>

using namespace vgui;

class ReplaysSettingsPage : public SettingsPage
{
    DECLARE_CLASS_SIMPLE(ReplaysSettingsPage, SettingsPage);

    ReplaysSettingsPage(Panel *pParent);

    ~ReplaysSettingsPage() {}

    void LoadSettings() OVERRIDE;

    void OnTextChanged(Panel *p) OVERRIDE;

    void OnControlModified(Panel *p) OVERRIDE;

private:
    void UpdateReplayEntityAlphaEntry() const;

    CCvarSlider *m_pReplayModelAlphaSlider;
    TextEntry *m_pReplayModelAlphaEntry;
};