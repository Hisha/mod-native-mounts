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

local function spellBookSlot(spellID)
 local wanted = GetSpellInfo(spellID)
 if not wanted then return nil end
 local tabs = GetNumSpellTabs()
 for tab = 1, tabs do
  local _, _, offset, count = GetSpellTabInfo(tab)
  for slot = offset + 1, offset + count do
   local name = GetSpellName(slot, BOOKTYPE_SPELL)
   if name == wanted then
    local link = GetSpellLink(slot, BOOKTYPE_SPELL)
    local linkedID = link and tonumber(string.match(link, "spell:(%d+)"))
    if not linkedID or linkedID == spellID then return slot end
   end
  end
 end
 return nil
end

local function refreshButton(button)
 local entry = button.mount
 local slot = spellBookSlot(entry.spell)
 button.spellBookSlot = slot
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
 if not button.spellBookSlot then return end
 -- Build 12340 uses the pre-4.0 spellbook-slot signature. The resulting cursor
 -- payload is a native spell action, not a module-owned action-bar surrogate.
 PickupSpell(button.spellBookSlot, BOOKTYPE_SPELL)
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
