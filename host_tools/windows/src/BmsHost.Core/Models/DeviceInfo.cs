namespace BmsHost.Core.Models;

public sealed record DeviceInfo(
    string SerialNumber,
    string HardwareVersion,
    string SoftwareVersion,
    string BluetoothName);
