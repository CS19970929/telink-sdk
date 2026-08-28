namespace BmsHost.Core.Protocol;

public static class BmsRegisterMap
{
    public const ushort BluetoothName = 0x0100;
    public const ushort BluetoothNameCount = 12;

    public const ushort ProductSerial = 0xC002;
    public const ushort ProductHardwareVersion = 0xC012;
    public const ushort ProductSoftwareVersion = 0xC022;
    public const ushort ProductStringRegisterCount = 16;

    public const ushort Cells = 0xD000;
    public const ushort CellCount = 10;
    public const ushort CellMax = 0xD020;
    public const ushort CellMin = 0xD021;
    public const ushort CellDelta = 0xD024;
    public const ushort PackVoltageDiv10 = 0xD025;
    public const ushort Temperature1 = 0xD026;
    public const ushort Temperature2 = 0xD027;
    public const ushort TemperatureMax = 0xD030;
    public const ushort TemperatureMin = 0xD031;
    public const ushort ChargeCurrentA10 = 0xD032;
    public const ushort DischargeCurrentA10 = 0xD033;
    public const ushort Soc = 0xD034;
    public const ushort Soh = 0xD035;

    public const ushort AfeStatus = 0xD115;
    public const ushort AfeFlags = 0xD116;

    public const ushort Realtime = 0xD120;
    public const ushort RealtimeCount = 11;
    public const ushort ProtectionStatus = 0xD130;
    public const ushort ProtectionStatusCount = 11;
    public const ushort SerialPm = 0xD140;
    public const ushort SerialPmCount = 10;

    public const ushort ProtectionLegacy = 0x2100;
    public const ushort ProtectionLegacyCount = 65;

    public const ushort Command = 0x1102;
}
