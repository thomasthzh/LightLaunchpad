namespace LightLaunchpad.Core.Tests;

internal static class TestAssert
{
    public static void Equal<T>(T expected, T actual)
    {
        if (!EqualityComparer<T>.Default.Equals(expected, actual))
        {
            throw new InvalidOperationException($"Expected '{expected}', got '{actual}'.");
        }
    }

    public static void False(bool value)
    {
        if (value)
        {
            throw new InvalidOperationException("Expected false, got true.");
        }
    }

    public static void True(bool value)
    {
        if (!value)
        {
            throw new InvalidOperationException("Expected true, got false.");
        }
    }

    public static void SequenceEqual<T>(IEnumerable<T> expected, IEnumerable<T> actual)
    {
        var expectedList = expected.ToList();
        var actualList = actual.ToList();

        if (expectedList.Count != actualList.Count)
        {
            throw new InvalidOperationException(
                $"Expected sequence length {expectedList.Count}, got {actualList.Count}.");
        }

        for (var index = 0; index < expectedList.Count; index++)
        {
            if (!EqualityComparer<T>.Default.Equals(expectedList[index], actualList[index]))
            {
                throw new InvalidOperationException(
                    $"Expected sequence item {index} to be '{expectedList[index]}', got '{actualList[index]}'.");
            }
        }
    }
}
