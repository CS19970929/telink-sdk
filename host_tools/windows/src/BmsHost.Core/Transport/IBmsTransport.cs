namespace BmsHost.Core.Transport;

public interface IBmsTransport : IAsyncDisposable
{
    string Name { get; }
    bool IsConnected { get; }
    event EventHandler? Disconnected;

    Task<byte[]> RequestAsync(
        ReadOnlyMemory<byte> request,
        TimeSpan timeout,
        CancellationToken cancellationToken = default);

    Task DisconnectAsync();
}
