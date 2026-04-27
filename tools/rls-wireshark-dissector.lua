--[[
--
-- Dissector for Radio Link Simulation Protocol
-- (UERANSIM project <https://github.com/aligungr/UERANSIM>).
--
-- CC0-1.0 2021 - Louis Royer (<https://github.com/louisroyer/RLS-wireshark-dissector>)
-- Updated 2024 - for extended RLS header (senderId/senderId2, lat/lon/alt position)
--
--]]

local rlsProtocol = Proto("RLS", "UERANSIM Radio Link Simulation (RLS) Protocol")
local fields = rlsProtocol.fields

local msgTypeNames = {
    [0] = "[Reserved]",
    [1] = "[Reserved]",
    [2] = "[Reserved]",
    [3] = "[Reserved]",
    [4] = "Heartbeat",
    [5] = "Heartbeat ACK",
    [6] = "PDU Transmission",
    [7] = "PDU Transmission ACK",
}

local pduTypeNames = {
    [0] = "[Reserved]",
    [1] = "RRC",
    [2] = "Data"
}

local rrcChannelNames = {
    [0] = "BCCH-BCH",
    [1] = "BCCH-DL-SCH",
    [2] = "DL-CCCH",
    [3] = "DL-DCCH",
    [4] = "PCCH",
    [5] = "UL-CCCH",
    [6] = "UL-CCCH1",
    [7] = "UL-DCCH",
}

local nrRrcDissectors = {
    [0] = "nr-rrc.bcch.bch",
    [1] = "nr-rrc.bcch.dl.sch",
    [2] = "nr-rrc.dl.ccch",
    [3] = "nr-rrc.dl.dcch",
    [4] = "nr-rrc.pcch",
    [5] = "nr-rrc.ul.ccch",
    [6] = "nr-rrc.ul.ccch1",
    [7] = "nr-rrc.ul.dcch",
}

-- Common header fields
fields.Version    = ProtoField.string("rls.version",     "Version")
fields.MsgType    = ProtoField.uint8( "rls.message_type","Message Type",           base.DEC, msgTypeNames)
fields.Sti        = ProtoField.uint64("rls.sti",         "Sender Node Temporary ID",base.DEC)
fields.SenderId   = ProtoField.uint32("rls.sender_id",   "Sender ID",              base.DEC)
fields.SenderId2  = ProtoField.uint32("rls.sender_id2",  "Sender ID 2",            base.DEC)

-- Heartbeat fields (position as IEEE 754 doubles, 8 bytes each)
fields.Latitude   = ProtoField.bytes("rls.latitude",    "Latitude (double)")
fields.Longitude  = ProtoField.bytes("rls.longitude",   "Longitude (double)")
fields.Altitude   = ProtoField.bytes("rls.altitude",    "Altitude (double)")

-- Heartbeat ACK fields
fields.Dbm        = ProtoField.int32("rls.dbm",          "RLS Signal Strength (dBm)", base.DEC)

-- PDU Transmission fields
fields.PduType    = ProtoField.uint8( "rls.pdu_type",    "PDU Type",               base.DEC, pduTypeNames)
fields.PduId      = ProtoField.uint32("rls.pdu_id",      "PDU ID",                 base.DEC)
fields.Payload    = ProtoField.uint32("rls.payload",     "Payload (RRC channel / PDU session ID)", base.DEC)
fields.RrcChannel = ProtoField.uint32("rls.rrc_channel", "RRC Channel",            base.DEC, rrcChannelNames)
fields.PduSessionId = ProtoField.uint32("rls.pdu_session_id", "PDU Session ID",    base.DEC)
fields.PduLength  = ProtoField.uint32("rls.pdu_length",  "PDU Length",             base.DEC)

-- PDU Transmission ACK fields
fields.AcknowledgeItem = ProtoField.uint32("rls.ack_item", "PDU ID")

function rlsProtocol.dissector(buffer, pinfo, tree)
    if buffer:len() == 0 then return end
    if buffer(0, 1):uint() ~= 0x03 then return end

    pinfo.cols.protocol = rlsProtocol.name

    local versionNumber = buffer(1, 1):uint() .. "." .. buffer(2, 1):uint() .. "." .. buffer(3, 1):uint()
    local subtree = tree:add(rlsProtocol, buffer(), "UERANSIM Radio Link Simulation (RLS) protocol")

    -- Common header: magic(1) + version(3) + msgType(1) + sti(8) + senderId(4) + senderId2(4) = 21 bytes
    subtree:add(fields.Version,   buffer(1, 3),  versionNumber)
    subtree:add(fields.MsgType,   buffer(4, 1))
    local msgType = buffer(4, 1):uint()
    subtree:add(fields.Sti,       buffer(5, 8))
    subtree:add(fields.SenderId,  buffer(13, 4))
    subtree:add(fields.SenderId2, buffer(17, 4))

    pinfo.cols.info = msgTypeNames[msgType]

    -- Message-specific fields start at offset 21
    if msgType == 4 then -- Heartbeat
        subtree:add(fields.Latitude,  buffer(21, 8))
        subtree:add(fields.Longitude, buffer(29, 8))
        subtree:add(fields.Altitude,  buffer(37, 8))

    elseif msgType == 5 then -- Heartbeat ACK
        subtree:add(fields.Dbm, buffer(21, 4))

    elseif msgType == 6 then -- PDU Transmission
        local pduType   = buffer(21, 1):uint()
        local payload   = buffer(26, 4):uint()
        local pduLength = buffer(30, 4):uint()

        subtree:add(fields.PduType,  buffer(21, 1))
        subtree:add(fields.PduId,    buffer(22, 4))

        if pduType == 1 then -- RRC: payload = RRC channel enum
            subtree:add(fields.RrcChannel, buffer(26, 4))
            subtree:add(fields.PduLength,  buffer(30, 4))
            local dissectorName = nrRrcDissectors[payload]
            if dissectorName then
                Dissector.get(dissectorName):call(buffer(34, pduLength):tvb(), pinfo, tree)
            end
        elseif pduType == 2 then -- Data: payload = PDU session ID
            subtree:add(fields.PduSessionId, buffer(26, 4))
            subtree:add(fields.PduLength,    buffer(30, 4))
            Dissector.get("ip"):call(buffer(34, pduLength):tvb(), pinfo, tree)
        else
            subtree:add(fields.Payload,   buffer(26, 4))
            subtree:add(fields.PduLength, buffer(30, 4))
        end

    elseif msgType == 7 then -- PDU Transmission ACK
        local ackCount = buffer(21, 4):uint()
        local ackArray = subtree:add(rlsProtocol, buffer(21, 4), "Acknowledge List (" .. ackCount .. ")")
        for i = 1, ackCount, 1 do
            ackArray:add(fields.AcknowledgeItem, buffer(25 + (i - 1) * 4, 4))
        end
    end
end

local udp_port = DissectorTable.get("udp.port")
udp_port:add(4997, rlsProtocol)
