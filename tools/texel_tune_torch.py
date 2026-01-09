import argparse
import pandas as pd
import numpy as np
import torch
import torch.nn as nn
import torch.optim as optim
from torch.utils.data import Dataset, DataLoader
import json
import time

# Dataset class
class ChessDataset(Dataset):
    def __init__(self, csv_file, device):
        print(f"Loading {csv_file}...")
        # Load only necessary columns? No, load all.
        # But for training, we need X (features) and Y (label).
        # CSV format: label,phase,score_mg,score_eg,eval, [f0_mg, f0_eg, f1_mg, f1_eg ...]

        # We need to drop label, phase, scores from X.
        # Actually, score_mg, score_eg are static eval components, usually not features unless used for residual learning.
        # Here we learn from scratch (or refine).
        # We will treat all columns after 'eval' as features.

        # Using float32 for speed and memory.
        self.df = pd.read_csv(csv_file, dtype=np.float32)
        print(f"Loaded {len(self.df)} rows.")

        self.y = torch.tensor(self.df['label'].values, dtype=torch.float32).unsqueeze(1).to(device)

        # Extract feature columns
        # Columns 0-4 are meta. 5+ are features.
        # Wait, let's verify column indices.
        # label, phase, score_mg, score_eg, eval -> 5 columns.
        # features start at index 5.
        feature_cols = self.df.columns[5:]
        self.feature_names = list(feature_cols)

        self.x = torch.tensor(self.df.iloc[:, 5:].values, dtype=torch.float32).to(device)
        self.len = len(self.df)

    def __len__(self):
        return self.len

    def __getitem__(self, idx):
        return self.x[idx], self.y[idx]

# Logistic Regression Model
class LogisticRegression(nn.Module):
    def __init__(self, input_dim, scaling_k=1.0/400.0):
        super(LogisticRegression, self).__init__()
        self.linear = nn.Linear(input_dim, 1, bias=False)
        self.scaling_k = scaling_k

        # Initialize weights to 0 or small random?
        # Ideally we initialize with current engine weights if we knew them.
        # But we learn from scratch or from data.
        # Zero init is safer for convex optimization (Logistic Regression).
        nn.init.zeros_(self.linear.weight)

    def forward(self, x):
        # Score = X @ W
        score = self.linear(x)
        # Prob = sigmoid(k * score)
        return torch.sigmoid(score * self.scaling_k)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--csv", required=True, help="Input CSV file from engine")
    parser.add_argument("--out", required=True, help="Output JSON weights file")
    parser.add_argument("--epochs", type=int, default=100)
    parser.add_argument("--lr", type=float, default=0.5) # High LR for logistic regression usually ok
    parser.add_argument("--batch", type=int, default=16384)
    args = parser.parse_args()

    device = "cuda" if torch.cuda.is_available() else "cpu"
    print(f"Using device: {device}")

    dataset = ChessDataset(args.csv, device)
    dataloader = DataLoader(dataset, batch_size=args.batch, shuffle=True)

    input_dim = dataset.x.shape[1]
    model = LogisticRegression(input_dim).to(device)

    criterion = nn.BCELoss()
    optimizer = optim.Adam(model.parameters(), lr=args.lr)

    start_time = time.time()

    for epoch in range(args.epochs):
        epoch_loss = 0
        count = 0
        for x_batch, y_batch in dataloader:
            optimizer.zero_grad()
            outputs = model(x_batch)
            loss = criterion(outputs, y_batch)
            loss.backward()
            optimizer.step()

            epoch_loss += loss.item() * x_batch.size(0)
            count += x_batch.size(0)

        print(f"Epoch {epoch+1}/{args.epochs}, Loss: {epoch_loss/count:.6f}")

    print(f"Training finished in {time.time() - start_time:.2f}s")

    # Extract weights
    weights = model.linear.weight.data.cpu().numpy()[0]

    # Save to JSON
    # Map feature name -> weight
    # Features are: name_mg, name_eg.
    # We want to reconstruct the parameters.
    # e.g. "mat_p_mg" -> weight.
    # We will save a dictionary "feature_name": value.
    # We should convert float weights to integers (cp)?
    # The feature output was (NetCount * Phase).
    # Model: P = sigmoid(1/400 * (W * Feat)).
    # So W is in centipawns.
    # We round to nearest integer.

    out_map = {}
    for name, w in zip(dataset.feature_names, weights):
        out_map[name] = int(round(w))

    with open(args.out, "w") as f:
        json.dump(out_map, f, indent=4)

    print(f"Weights saved to {args.out}")

if __name__ == "__main__":
    main()
