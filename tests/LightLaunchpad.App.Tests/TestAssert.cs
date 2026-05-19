namespace LightLaunchpad.App.Tests;

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

    public static void Same(object expected, object actual)
    {
        if (!ReferenceEquals(expected, actual))
        {
            throw new InvalidOperationException("Expected references to be the same.");
        }
    }

    public static void NotSame(object expected, object actual)
    {
        if (ReferenceEquals(expected, actual))
        {
            throw new InvalidOperationException("Expected references to be different.");
        }
    }

    public static void Contains(string expectedSubstring, string actual)
    {
        if (!actual.Contains(expectedSubstring, StringComparison.Ordinal))
        {
            throw new InvalidOperationException($"Expected text to contain '{expectedSubstring}'.");
        }
    }

    public static void DoesNotContain(string unexpectedSubstring, string actual)
    {
        if (actual.Contains(unexpectedSubstring, StringComparison.Ordinal))
        {
            throw new InvalidOperationException($"Expected text not to contain '{unexpectedSubstring}'.");
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

    public static void SequenceSame<T>(IEnumerable<T> expected, IEnumerable<T> actual) where T : class
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
            if (!ReferenceEquals(expectedList[index], actualList[index]))
            {
                throw new InvalidOperationException($"Expected sequence item {index} to be the same reference.");
            }
        }
    }
}
