namespace LightLaunchpad.Core.Layout;

public static class DragInsertionCalculator
{
    public static int FindInsertionIndex(
        IReadOnlyList<DragTileBounds> tiles,
        double pointerX,
        double pointerY)
    {
        if (tiles.Count == 0)
        {
            return 0;
        }

        var ordered = tiles
            .Select((tile, index) => new OrderedTile(tile, index))
            .OrderBy(tile => tile.Bounds.CenterY)
            .ThenBy(tile => tile.Bounds.X)
            .ToList();
        var rows = BuildRows(ordered);
        var row = rows
            .Where(candidate => pointerY >= candidate.Top && pointerY <= candidate.Bottom)
            .OrderBy(candidate => Math.Abs(pointerY - candidate.CenterY))
            .FirstOrDefault()
            ?? rows.MinBy(candidate => Math.Abs(pointerY - candidate.CenterY))!;

        var rowTiles = row.Tiles.OrderBy(tile => tile.Bounds.X).ToList();
        foreach (var tile in rowTiles)
        {
            if (pointerX < tile.Bounds.CenterX)
            {
                return tile.VisualIndex;
            }
        }

        return rowTiles[^1].VisualIndex + 1;
    }

    private static List<TileRow> BuildRows(IReadOnlyList<OrderedTile> ordered)
    {
        var rows = new List<TileRow>();
        foreach (var tile in ordered)
        {
            var row = rows.LastOrDefault();
            if (row is null || tile.Bounds.CenterY > row.Bottom)
            {
                rows.Add(new TileRow([tile]));
            }
            else
            {
                row.Add(tile);
            }
        }

        for (var rowIndex = 0; rowIndex < rows.Count; rowIndex++)
        {
            rows[rowIndex].SetVisualStart(rows.Take(rowIndex).Sum(row => row.Tiles.Count));
        }

        return rows;
    }

    private sealed class TileRow
    {
        public TileRow(List<OrderedTile> tiles)
        {
            Tiles = tiles;
        }

        public List<OrderedTile> Tiles { get; }

        public double Top => Tiles.Min(tile => tile.Bounds.Y);

        public double Bottom => Tiles.Max(tile => tile.Bounds.Bottom);

        public double CenterY => Tiles.Average(tile => tile.Bounds.CenterY);

        public void Add(OrderedTile tile) => Tiles.Add(tile);

        public void SetVisualStart(int visualStart)
        {
            var orderedTiles = Tiles.OrderBy(tile => tile.Bounds.X).ToList();
            for (var index = 0; index < orderedTiles.Count; index++)
            {
                orderedTiles[index].VisualIndex = visualStart + index;
            }
        }
    }

    private sealed class OrderedTile
    {
        public OrderedTile(DragTileBounds bounds, int sourceIndex)
        {
            Bounds = bounds;
            SourceIndex = sourceIndex;
        }

        public DragTileBounds Bounds { get; }

        public int SourceIndex { get; }

        public int VisualIndex { get; set; }
    }
}

public readonly record struct DragTileBounds(double X, double Y, double Width, double Height)
{
    public double Bottom => Y + Height;

    public double CenterX => X + Width / 2;

    public double CenterY => Y + Height / 2;
}
