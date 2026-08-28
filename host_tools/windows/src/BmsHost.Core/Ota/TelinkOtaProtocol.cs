using System.Buffers.Binary;

namespace BmsHost.Core.Ota;

public static class TelinkOtaProtocol
{
    public const ushort CommandStart = 0xFF01;
    public const ushort CommandEnd = 0xFF02;
    public const ushort CommandResult = 0xFF06;
    public const int DataBytesPerPdu = 16;

    public static byte[] BuildLegacyStart() => [0x01, 0xFF];

    public static IReadOnlyList<byte[]> BuildLegacyDataPackets(ReadOnlySpan<byte> firmware)
    {
        var packetCount = (firmware.Length + DataBytesPerPdu - 1) / DataBytesPerPdu;
        if (packetCount == 0 || packetCount > ushort.MaxValue)
            throw new InvalidDataException("Firmware size is outside legacy Telink OTA packet range.");

        var packets = new List<byte[]>(packetCount);
        for (var index = 0; index < packetCount; index++)
        {
            var packet = new byte[20];
            BinaryPrimitives.WriteUInt16LittleEndian(packet.AsSpan(0, 2), (ushort)index);
            packet.AsSpan(2, DataBytesPerPdu).Fill(0xFF);
            var srcOffset = index * DataBytesPerPdu;
            var count = Math.Min(DataBytesPerPdu, firmware.Length - srcOffset);
            firmware.Slice(srcOffset, count).CopyTo(packet.AsSpan(2, count));
            var crc = ComputeCrc16(packet.AsSpan(0, 18));
            BinaryPrimitives.WriteUInt16LittleEndian(packet.AsSpan(18, 2), crc);
            packets.Add(packet);
        }
        return packets;
    }

    public static byte[] BuildEnd(ushort lastAddressIndex)
    {
        var inverse = (ushort)(lastAddressIndex ^ 0xFFFF);
        return
        [
            0x02, 0xFF,
            (byte)lastAddressIndex, (byte)(lastAddressIndex >> 8),
            (byte)inverse, (byte)(inverse >> 8),
        ];
    }

    public static int ResolveFirmwareLength(ReadOnlySpan<byte> bin)
    {
        if (bin.Length < 0x1C) throw new InvalidDataException("Firmware BIN is too short.");
        var declared = BinaryPrimitives.ReadUInt32LittleEndian(bin.Slice(0x18, 4));
        if (declared >= 0x20 && declared <= bin.Length)
            return checked((int)declared);
        return bin.Length;
    }

    public static bool TryParseResult(ReadOnlySpan<byte> payload, out byte result)
    {
        result = 0xFF;
        if (payload.Length < 3) return false;
        if (BinaryPrimitives.ReadUInt16LittleEndian(payload) != CommandResult) return false;
        result = payload[2];
        return true;
    }

    public static ushort ComputeCrc16(ReadOnlySpan<byte> data)
    {
        ushort crc = 0xFFFF;
        foreach (var value in data)
        {
            var ds = value;
            for (var bit = 0; bit < 8; bit++)
            {
                crc = (ushort)((crc >> 1) ^ (((crc ^ ds) & 1) != 0 ? 0xA001 : 0));
                ds >>= 1;
            }
        }
        return crc;
    }
}
