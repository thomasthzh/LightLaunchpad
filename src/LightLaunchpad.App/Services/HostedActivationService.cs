using System.Threading;
using System.Windows.Threading;
using LightLaunchpad.Core.Activation;

namespace LightLaunchpad.App.Services;

public sealed class HostedActivationService : IDisposable
{
    private readonly LaunchpadActivationContext _context;
    private readonly Dispatcher _dispatcher;
    private readonly Action _show;
    private readonly Action _exit;
    private readonly ManualResetEventSlim _shutdown = new(false);
    private EventWaitHandle? _showEvent;
    private EventWaitHandle? _exitEvent;
    private Thread? _showThread;
    private Thread? _exitThread;
    private bool _disposed;

    public HostedActivationService(
        LaunchpadActivationContext context,
        Dispatcher dispatcher,
        Action show,
        Action exit)
    {
        _context = context;
        _dispatcher = dispatcher;
        _show = show;
        _exit = exit;
    }

    public void Start()
    {
        if (!string.IsNullOrWhiteSpace(_context.ShowEventName))
        {
            _showEvent = EventWaitHandle.OpenExisting(_context.ShowEventName);
            _showThread = StartListener("LightLaunchpad-HostedShow", _showEvent, _show);
        }

        if (!string.IsNullOrWhiteSpace(_context.ExitEventName))
        {
            _exitEvent = EventWaitHandle.OpenExisting(_context.ExitEventName);
            _exitThread = StartListener("LightLaunchpad-HostedExit", _exitEvent, _exit);
        }
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;
        _shutdown.Set();
        Join(ref _showThread);
        Join(ref _exitThread);
        _showEvent?.Dispose();
        _exitEvent?.Dispose();
        _shutdown.Dispose();
    }

    private Thread StartListener(string name, EventWaitHandle signal, Action action)
    {
        var thread = new Thread(() => Listen(signal, action))
        {
            IsBackground = true,
            Name = name
        };
        thread.Start();
        return thread;
    }

    private void Listen(EventWaitHandle signal, Action action)
    {
        var handles = new WaitHandle[] { signal, _shutdown.WaitHandle };
        try
        {
            while (!_shutdown.IsSet)
            {
                var index = WaitHandle.WaitAny(handles);
                if (index == 0)
                {
                    _dispatcher.BeginInvoke(action);
                }
                else
                {
                    return;
                }
            }
        }
        catch (ObjectDisposedException)
        {
        }
        catch (ThreadInterruptedException)
        {
        }
    }

    private static void Join(ref Thread? thread)
    {
        if (thread is null)
        {
            return;
        }

        if (!thread.Join(TimeSpan.FromMilliseconds(300)))
        {
            thread.Interrupt();
            thread.Join(TimeSpan.FromMilliseconds(300));
        }

        thread = null;
    }
}