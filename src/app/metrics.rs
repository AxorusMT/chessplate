use crate::engine::SearchSnapshot;

pub fn best_move_stability(history: &[SearchSnapshot]) -> f32 {
    if history.len() <= 1 {
        return 1.0;
    }

    let tail = history.iter().rev().take(12).collect::<Vec<_>>();
    let tail = tail.into_iter().rev().collect::<Vec<_>>();
    let Some(reference) = tail.last().map(|snapshot| snapshot.best_move) else {
        return 1.0;
    };

    let matches = tail
        .iter()
        .filter(|snapshot| snapshot.best_move == reference)
        .count();
    matches as f32 / tail.len() as f32
}

pub fn eval_volatility_cp(history: &[SearchSnapshot]) -> f32 {
    let tail = history.iter().rev().take(14).collect::<Vec<_>>();
    if tail.len() < 2 {
        return 0.0;
    }

    let mut total = 0.0;
    let mut count = 0.0;
    for pair in tail.windows(2) {
        let a = pair[0].best_score_cp as f32;
        let b = pair[1].best_score_cp as f32;
        total += (a - b).abs();
        count += 1.0;
    }

    if count == 0.0 { 0.0 } else { total / count }
}

pub fn format_big_number(value: u64) -> String {
    if value >= 1_000_000_000 {
        format!("{:.2}B", value as f64 / 1_000_000_000.0)
    } else if value >= 1_000_000 {
        format!("{:.2}M", value as f64 / 1_000_000.0)
    } else if value >= 1_000 {
        format!("{:.1}K", value as f64 / 1_000.0)
    } else {
        value.to_string()
    }
}
