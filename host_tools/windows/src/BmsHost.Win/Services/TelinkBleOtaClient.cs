using BmsHost.Core.Ota;
using BmsHost.Win.Transport;

namespace BmsHost.Win.Services;

public sealed record OtaProgress(int SentPackets, int TotalPackets, int SentBytes, int FirmwareBytes)
{
    public double Percent => FirmwareBytes == 0 ? 0 : Math.Min(100.0, SentBytes * 100.0 / FirmwareBytes);
}

public sealed record OtaCompletion(bool ResultConfirmed, byte? ResultCode, string Message);

public sealed class TelinkBleOtaClient
{
    private readonly BleBmsTransport _transport;
    public TelinkBleOtaClient(BleBmsTransport transport) => _transport = transport;

    public async Task<OtaCompletion> UpgradeAsync(string filePath, IProgress<OtaProgress>? progress = null, CancellationToken cancellationToken = default)
    {
        var fullBin = await File.ReadAllBytesAsync(filePath, cancellationToken);
        var firmwareLength = TelinkOtaProtocol.ResolveFirmwareLength(fullBin);
        var firmware = fullBin.AsSpan(0, firmwareLength).ToArray();
        var packets = TelinkOtaProtocol.BuildLegacyDataPackets(firmware);
        if (packets.Count == 0) throw new InvalidDataException("Firmware contains no OTA data.");

        await _transport.PrepareOtaAsync(cancellationToken);
        await _transport.WriteOtaAsync(TelinkOtaProtocol.BuildLegacyStart(), withResponse: true, cancellationToken);
        await Task.Delay(30, cancellationToken);

        for (var i = 0; i < packets.Count; i++)
        {
            await _transport.WriteOtaAsync(packets[i], withResponse: false, cancellationToken);
            // Pace WriteWithoutResponse so the Windows GATT queue does not
            // outrun the B85 controller/flash writer. Tune only after bench data.
            if ((i & 0x0F) == 0x0F) await Task.Delay(12, cancellationToken);
            else if ((i & 0x03) == 0x03) await Task.Delay(2, cancellationToken);

            progress?.Report(new OtaProgress(
                i + 1, packets.Count,
                Math.Min((i + 1) * TelinkOtaProtocol.DataBytesPerPdu, firmwareLength),
                firmwareLength));
        }

        var lastIndex = checked((ushort)(packets.Count - 1));
        await _transport.WriteOtaAsync(TelinkOtaProtocol.BuildEnd(lastIndex), withResponse: true, cancellationToken);
        var result = await _transport.WaitForOtaResultAsync(TimeSpan.FromSeconds(8), cancellationToken);
        if (result.HasValue)
        {
            return result.Value == 0
                ? new OtaCompletion(true, result, "OTA server reported success. Device should reboot into the new image.")
                : new OtaCompletion(true, result, $"OTA server reported failure code 0x{result.Value:X2}.");
        }
        return new OtaCompletion(false, null,
            "OTA end was sent. No result notification was observed; reconnect and verify device identity/behavior after reboot.");
    }
}
