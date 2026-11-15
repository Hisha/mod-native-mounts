-- Complete native mount Collections page for build 12340.
local PREFIX, VERSION = "NMOUNTS", "1"
local CATALOG = NATIVE_MOUNT_CATALOG or {}
local ROWS, ROW_HEIGHT = 9, 54
local buttons, ownedSpells = {}, {}
local offset, ownershipReady, selectedSpell = 0, false, nil
local nextRequestId, activeRequestId = 0, 0
local raceIds = {Human=1,Orc=2,Dwarf=3,NightElf=4,Scourge=5,Tauren=6,Gnome=7,Troll=8,BloodElf=10,Draenei=11}

local function mountCompanionSlot(spellID)
 for slot = 1, GetNumCompanions("MOUNT") do
  local _, _, companionSpellID = GetCompanionInfo("MOUNT", slot)
  if companionSpellID == spellID then return slot end
 end
 return nil
end

local function maskHas(mask, id)
 return id and id > 0 and bit and bit.band(mask, bit.lshift(1, id - 1)) ~= 0
end

local function chooseVariant(entry)
 local _, raceToken = UnitRace("player"); local _, _, classId = UnitClass("player")
 local raceId = raceIds[raceToken]
 for _, variant in ipairs(entry.variants) do
  if maskHas(variant.races, raceId) and maskHas(variant.classes, classId) and
     mountCompanionSlot(variant.spell) then return variant end
 end
 for _, variant in ipairs(entry.variants) do
  if maskHas(variant.races, raceId) and maskHas(variant.classes, classId) then return variant end
 end
 for _, variant in ipairs(entry.variants) do
  if mountCompanionSlot(variant.spell) then return variant end
 end
 return entry.variants[1]
end

local function entryOwned(entry)
 for _, variant in ipairs(entry.variants) do if ownedSpells[variant.spell] then return true end end
 return false
end

local function refreshButton(button)
 local entry = button.mount
 if not entry then button:Hide(); return end
 button:Show()
 local variant = chooseVariant(entry); button.variant = variant
 local slot = mountCompanionSlot(variant.spell)
 button.companionSlot = slot
 local name, _, icon = GetSpellInfo(variant.spell)
 button.icon:SetTexture(icon or "Interface\\Icons\\INV_Misc_QuestionMark")
 button.name:SetText(name or variant.name)
 if slot then
  button.icon:SetDesaturated(false)
  button.status:SetText("Collected - drag to an action bar")
  button.status:SetTextColor(.2, 1, .2)
 elseif ownershipReady and entryOwned(entry) then
  button.icon:SetDesaturated(false)
  button.status:SetText("Collected - restricted on this character")
  button.status:SetTextColor(1, .72, .2)
 else
  button.icon:SetDesaturated(true)
  button.status:SetText(ownershipReady and "Uncollected" or "Loading account ownership...")
  button.status:SetTextColor(.65, .65, .65)
 end
end

local function refreshRows()
 for row, button in ipairs(buttons) do
  button.mount = CATALOG[offset + row]
  refreshButton(button)
 end
 if NativeMountsScrollBar then
  NativeMountsScrollBar:SetMinMaxValues(0, math.max(0, #CATALOG - ROWS))
  NativeMountsScrollBar:SetValue(offset)
 end
 NativeMountsCount:SetText(string.format("%d mounts", #CATALOG))
end

local function setOffset(value)
 offset = math.max(0, math.min(math.floor(value + .5), math.max(0, #CATALOG - ROWS)))
 refreshRows()
end

function NativeMounts_Row_OnDragStart(button)
 local slot = button.variant and mountCompanionSlot(button.variant.spell)
 if not slot then refreshButton(button); return end
 PickupCompanion("MOUNT", slot)
end

function NativeMounts_Row_OnClick(button)
 if not button.mount then return end
 local variant = chooseVariant(button.mount); button.variant = variant
 selectedSpell = variant.spell
 NativeMountsPreview:ClearModel()
 if variant.creature and variant.creature > 0 then
  NativeMountsPreview:SetCreature(variant.creature)
  NativeMountsPreview:SetFacing(0); NativeMountsPreview:SetModelScale(1)
  NativeMountsPreview:SetPosition(0, 0, 0); NativeMountsPreviewUnavailable:Hide()
 else
  NativeMountsPreview:ClearModel(); NativeMountsPreviewUnavailable:Show()
 end
 NativeMountsSelectedName:SetText(variant.name)
 if ownershipReady and entryOwned(button.mount) and not mountCompanionSlot(variant.spell) then
  NativeMountsRestriction:SetText("This account owns the mount, but this character does not meet its class, faction, profession, level, or riding requirement.")
 else NativeMountsRestriction:SetText("") end
end

function NativeMounts_Row_OnEnter(button)
 if not button.mount then return end
 GameTooltip:SetOwner(button, "ANCHOR_RIGHT")
 GameTooltip:SetHyperlink("spell:" .. button.variant.spell); GameTooltip:Show()
end

local function requestOwnership()
 if SendAddonMessage and UnitName("player") then
  nextRequestId = nextRequestId + 1
  activeRequestId = nextRequestId
  ownershipReady = false
  refreshRows()
  SendAddonMessage(PREFIX, VERSION .. "\tSYNC\t" .. activeRequestId, "WHISPER", UnitName("player"))
 end
end

local function handleAddon(prefix, message, channel, sender)
 if prefix ~= PREFIX or channel ~= "WHISPER" or sender ~= UnitName("player") then return end
 local version, operation, request, body = message:match("^(%d+)\t([^\t]+)\t(%d+)\t?(.*)$")
 if version ~= VERSION then return end
 if tonumber(request) ~= activeRequestId then return end
 if operation == "RESET" then ownedSpells = {}; ownershipReady = false
 elseif operation == "OWN" then
  for value in body:gmatch("%d+") do ownedSpells[tonumber(value)] = true end
 elseif operation == "END" then
  ownershipReady = true; refreshRows()
  if selectedSpell then
   for _, button in ipairs(buttons) do
    if button.variant and button.variant.spell == selectedSpell then NativeMounts_Row_OnClick(button) end
   end
  end
 end
end

function NativeMounts_OnLoad(frame)
 if RegisterAddonMessagePrefix then RegisterAddonMessagePrefix(PREFIX) end
 frame:RegisterEvent("SPELLS_CHANGED"); frame:RegisterEvent("COMPANION_LEARNED")
 frame:RegisterEvent("COMPANION_UNLEARNED"); frame:RegisterEvent("COMPANION_UPDATE")
 frame:RegisterEvent("CHAT_MSG_ADDON")
 frame:SetScript("OnEvent", function(self, event, ...)
  if event == "CHAT_MSG_ADDON" then handleAddon(...) elseif self:IsShown() then refreshRows() end
 end)
 frame:EnableMouseWheel(true)
 frame:SetScript("OnMouseWheel", function(_, delta) setOffset(offset - delta * 3) end)
 for row = 1, ROWS do
  local button = CreateFrame("Button", "NativeMountsRow" .. row, frame)
  button:SetWidth(330); button:SetHeight(ROW_HEIGHT)
  button:SetPoint("TOPLEFT", 18, -18 - ((row - 1) * ROW_HEIGHT))
  button:RegisterForDrag("LeftButton"); button:SetScript("OnDragStart", NativeMounts_Row_OnDragStart)
  button:SetScript("OnClick", NativeMounts_Row_OnClick); button:SetScript("OnEnter", NativeMounts_Row_OnEnter)
  button:SetScript("OnLeave", function() GameTooltip:Hide() end)
  button.icon = button:CreateTexture(nil, "ARTWORK"); button.icon:SetWidth(42); button.icon:SetHeight(42)
  button.icon:SetPoint("LEFT", 4, 0)
  button.name = button:CreateFontString(nil, "ARTWORK", "GameFontNormal")
  button.name:SetPoint("TOPLEFT", button.icon, "TOPRIGHT", 9, -5)
  button.status = button:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall")
  button.status:SetPoint("TOPLEFT", button.name, "BOTTOMLEFT", 0, -6)
  button:SetHighlightTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight")
  buttons[row] = button
 end
 local scroll = CreateFrame("Slider", "NativeMountsScrollBar", frame, "UIPanelScrollBarTemplate")
 scroll:SetPoint("TOPLEFT", 350, -34); scroll:SetPoint("BOTTOMLEFT", 350, 70)
 scroll:SetValueStep(1); scroll:SetScript("OnValueChanged", function(_, value) setOffset(value) end)
 refreshRows()
end

function NativeMounts_OpenCollections()
 NativeMountsCollectionsFrame:Show(); refreshRows(); requestOwnership()
 if not selectedSpell and buttons[1] and buttons[1].mount then NativeMounts_Row_OnClick(buttons[1]) end
end

function NativeMounts_CloseCollections() NativeMountsCollectionsFrame:Hide() end
