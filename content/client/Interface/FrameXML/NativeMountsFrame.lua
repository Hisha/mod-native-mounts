-- Native Mounts account-grant proof of concept for build 12340.
-- Static catalog data is intentionally non-authoritative; the server owns all
-- ownership and eligibility decisions.
local POC_MOUNTS = {
 { spell=58983, creature=31319, label="Big Blizzard Bear" },
 { spell=59567, creature=31694, label="Azure Drake" },
 { spell=23161, creature=14505, label="Dreadsteed" },
 { spell=44151, creature=24654, label="Turbo-Charged Flying Machine" },
 { spell=32243, creature=18363, label="Tawny Wind Rider" },
}

local buttons = {}

local function mountCompanionSlot(spellID)
 for slot = 1, GetNumCompanions("MOUNT") do
  -- Build 12340 keeps learned mounts in the companion spellbook. Its third
  -- return is the real mount spell ID; the companion slot is only an unstable
  -- index into the native Mounts window.
  local _, _, companionSpellID = GetCompanionInfo("MOUNT", slot)
  if companionSpellID == spellID then return slot end
 end
 return nil
end

local function refreshButton(button)
 local entry = button.mount
 local slot = mountCompanionSlot(entry.spell)
 button.companionSlot = slot
 local name, _, icon = GetSpellInfo(entry.spell)
 button.icon:SetTexture(icon or "Interface\\Icons\\INV_Misc_QuestionMark")
 button.name:SetText(name or entry.label)
 if slot then
  button.icon:SetDesaturated(false)
  button.status:SetText("Collected - drag to an action bar")
  button.status:SetTextColor(.2, 1, .2)
 else
  button.icon:SetDesaturated(true)
  button.status:SetText("Not available to this character")
  button.status:SetTextColor(.65, .65, .65)
 end
end

function NativeMounts_Row_OnDragStart(button)
 -- Companion indices can be reordered, so resolve the exact spell again at
 -- drag time instead of trusting the slot captured by the last refresh.
 local slot = mountCompanionSlot(button.mount.spell)
 if not slot then
  button.companionSlot = nil
  refreshButton(button)
  return
 end
 button.companionSlot = slot
 PickupCompanion("MOUNT", slot)
end

function NativeMounts_Row_OnClick(button)
 NativeMountsPreview:SetCreature(button.mount.creature)
 NativeMountsPreview:SetFacing(0)
 NativeMountsPreview:SetModelScale(1)
 NativeMountsPreview:SetPosition(0, 0, 0)
 NativeMountsSelectedName:SetText(button.mount.label)
end

function NativeMounts_Row_OnEnter(button)
 GameTooltip:SetOwner(button, "ANCHOR_RIGHT")
 GameTooltip:SetHyperlink("spell:" .. button.mount.spell)
 GameTooltip:Show()
end

function NativeMounts_OnLoad(frame)
 frame:RegisterEvent("SPELLS_CHANGED")
 frame:RegisterEvent("COMPANION_LEARNED")
 frame:RegisterEvent("COMPANION_UNLEARNED")
 frame:RegisterEvent("COMPANION_UPDATE")
 frame:SetScript("OnEvent", function(self)
  if self:IsShown() then
   for _, button in ipairs(buttons) do refreshButton(button) end
  end
 end)
 for index, entry in ipairs(POC_MOUNTS) do
  local button = CreateFrame("Button", "NativeMountsPocRow" .. index, frame)
  button:SetWidth(330); button:SetHeight(54)
  button:SetPoint("TOPLEFT", 18, -18 - ((index - 1) * 62))
  button:RegisterForDrag("LeftButton")
  button:SetScript("OnDragStart", NativeMounts_Row_OnDragStart)
  button:SetScript("OnClick", NativeMounts_Row_OnClick)
  button:SetScript("OnEnter", NativeMounts_Row_OnEnter)
  button:SetScript("OnLeave", function() GameTooltip:Hide() end)
  button.mount = entry
  button.icon = button:CreateTexture(nil, "ARTWORK")
  button.icon:SetWidth(44); button.icon:SetHeight(44); button.icon:SetPoint("LEFT", 4, 0)
  button.name = button:CreateFontString(nil, "ARTWORK", "GameFontNormal")
  button.name:SetPoint("TOPLEFT", button.icon, "TOPRIGHT", 10, -5)
  button.status = button:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall")
  button.status:SetPoint("TOPLEFT", button.name, "BOTTOMLEFT", 0, -6)
  button:SetHighlightTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight")
  buttons[index] = button
 end
end

function NativeMounts_OpenCollections()
 NativeMountsCollectionsFrame:Show()
 for _, button in ipairs(buttons) do refreshButton(button) end
 local selectedName = NativeMountsSelectedName:GetText()
 if not selectedName or selectedName == "" then NativeMounts_Row_OnClick(buttons[1]) end
end

function NativeMounts_CloseCollections()
 NativeMountsCollectionsFrame:Hide()
end
