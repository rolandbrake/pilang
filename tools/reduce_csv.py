"""
    reduce the CSV file to a smaller size by reducing the number of rows
    
    USAGE[1]: python reduce_csv.py input.csv 5000 output.csv
    USAGE[2]: python reduce_csv.py input.csv 5000 output.csv --random
    USAGE[3]: python reduce_csv.py input.csv 5000 output.csv --random --seed 123
    
"""

import pandas as pd
import argparse

def reduce_csv(input_file, output_file, num_rows, random_select=False, random_state=42):
    # Read CSV
    df = pd.read_csv(input_file)

    if num_rows > len(df):
        raise ValueError(
            f"Requested {num_rows} rows, but file contains only {len(df)} rows."
        )

    if random_select:
        # Pick rows from random positions
        reduced_df = df.sample(n=num_rows, random_state=random_state)
    else:
        # Take first N rows
        reduced_df = df.head(num_rows)

    reduced_df.to_csv(output_file, index=False)

    print(
        f"Saved {len(reduced_df)} rows to {output_file} "
        f"({'random sample' if random_select else 'first rows'})"
    )

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Reduce CSV dataset size.")

    parser.add_argument("input_csv", help="Input CSV file")
    parser.add_argument("num_rows", type=int, help="Number of rows to keep")
    parser.add_argument("output_csv", help="Output CSV file")

    parser.add_argument(
        "--random",
        action="store_true",
        help="Select rows from random positions instead of taking the first N rows"
    )

    parser.add_argument(
        "--seed",
        type=int,
        default=42,
        help="Random seed (used only with --random)"
    )

    args = parser.parse_args()

    reduce_csv(
        args.input_csv,
        args.output_csv,
        args.num_rows,
        random_select=args.random,
        random_state=args.seed
    )