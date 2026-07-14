import { Component, OnDestroy, OnInit } from '@angular/core';
import { Subject, Subscription, finalize, switchMap, takeUntil, takeWhile, timer } from 'rxjs';

import { AsicTuningChipResult, AsicTuningStatus } from 'src/app/generated/models';
import { SystemApiService } from 'src/app/services/system.service';

interface CoreScanView {
  coreId: number;
  passCount: number;
  hashrate: number;
  deviationLabel: string;
  heatColor: string;
  level: 'above-mean' | 'below-mean' | 'at-mean' | 'silent';
}

interface ChipScanView {
  result: AsicTuningChipResult;
  cores: CoreScanView[];
  meanHashrate: number;
}

@Component({
  selector: 'app-tuning',
  templateUrl: './tuning.component.html',
  styleUrls: ['./tuning.component.scss'],
  standalone: false
})
export class TuningComponent implements OnInit, OnDestroy {
  status: AsicTuningStatus | null = null;
  chipViews: ChipScanView[] = [];
  selectedChipId = 0;
  isStarting = false;
  errorMessage = '';

  private scanSubscription?: Subscription;
  private destroy$ = new Subject<void>();

  constructor(private systemService: SystemApiService) {}

  ngOnInit(): void {
    this.systemService.getTuningStatus()
      .pipe(takeUntil(this.destroy$))
      .subscribe({
        next: status => {
          this.setStatus(status);
          if (this.shouldPoll(status)) {
            this.watchScan();
          }
        },
        error: () => this.errorMessage = 'Unable to read tuning status'
      });
  }

  ngOnDestroy(): void {
    this.scanSubscription?.unsubscribe();
    this.destroy$.next();
    this.destroy$.complete();
  }

  startScan(): void {
    if (!this.canStart) {
      return;
    }

    this.isStarting = true;
    this.errorMessage = '';
    this.systemService.startTuning()
      .pipe(
        finalize(() => this.isStarting = false),
        takeUntil(this.destroy$)
      )
      .subscribe({
        next: status => {
          this.setStatus(status);
          this.watchScan();
        },
        error: () => this.errorMessage = 'Unable to start the core scan'
      });
  }

  selectChip(chipId: number): void {
    this.selectedChipId = chipId;
  }

  get selectedChip(): ChipScanView | undefined {
    return this.chipViews.find(chip => chip.result.chipId === this.selectedChipId);
  }

  get canStart(): boolean {
    return !!this.status?.supported && this.status.chipCount > 0 && !this.isStarting && !this.isActive(this.status);
  }

  get statusLabel(): string {
    switch (this.status?.state) {
      case 'queued': return 'Queued';
      case 'measuring': return 'Measuring';
      case 'reading': return 'Reading';
      case 'complete': return this.status.validated ? 'Validated' : 'Check Results';
      case 'error': return 'Error';
      case 'unsupported': return 'Unavailable';
      default: return 'Ready';
    }
  }

  trackCore(_: number, core: CoreScanView): number {
    return core.coreId;
  }

  private watchScan(): void {
    this.scanSubscription?.unsubscribe();
    this.scanSubscription = timer(750, 750).pipe(
      switchMap(() => this.systemService.getTuningStatus()),
      takeWhile(status => this.shouldPoll(status), true),
      takeUntil(this.destroy$)
    ).subscribe({
      next: status => this.setStatus(status),
      error: () => this.errorMessage = 'The core scan status could not be refreshed'
    });
  }

  private setStatus(status: AsicTuningStatus): void {
    this.status = status;
    if (status.state !== 'complete') {
      this.chipViews = [];
      return;
    }

    const hashesPerPass = Math.pow(2, status.leadingZeros);
    const hashratePerPass = hashesPerPass / status.runtimeSeconds / 1e9;
    this.chipViews = status.chips.map(result => {
      const rates = result.corePassCounts.map(count => count * hashratePerPass);
      const meanHashrate = this.mean(rates);
      const cores = rates.map((hashrate, coreId): CoreScanView => {
        const deviationPercent = meanHashrate > 0
          ? ((hashrate - meanHashrate) / meanHashrate) * 100
          : 0;
        return {
          coreId,
          passCount: result.corePassCounts[coreId],
          hashrate,
          deviationLabel: `${deviationPercent >= 0 ? '+' : ''}${deviationPercent.toFixed(2)}%`,
          heatColor: this.coreHeatColor(hashrate, meanHashrate),
          level: this.coreLevel(hashrate, meanHashrate)
        };
      });
      return { result, cores, meanHashrate };
    });

    if (!this.chipViews.some(chip => chip.result.chipId === this.selectedChipId)) {
      this.selectedChipId = this.chipViews[0]?.result.chipId ?? 0;
    }
  }

  private isActive(status: AsicTuningStatus): boolean {
    return status.state === 'queued' || status.state === 'measuring' || status.state === 'reading';
  }

  private shouldPoll(status: AsicTuningStatus): boolean {
    return this.isActive(status) || (status.supported && status.chipCount === 0);
  }

  private mean(values: number[]): number {
    if (values.length === 0) {
      return 0;
    }
    return values.reduce((sum, value) => sum + value, 0) / values.length;
  }

  private coreLevel(hashrate: number, meanHashrate: number): CoreScanView['level'] {
    if (meanHashrate <= 0) {
      return 'silent';
    }
    const deviationPercent = ((hashrate - meanHashrate) / meanHashrate) * 100;
    if (Math.abs(deviationPercent) < 0.05) {
      return 'at-mean';
    }
    return deviationPercent > 0 ? 'above-mean' : 'below-mean';
  }

  private coreHeatColor(hashrate: number, meanHashrate: number): string {
    if (meanHashrate <= 0) {
      return 'var(--surface-300)';
    }

    const signedDeviationRatio = (hashrate - meanHashrate) / meanHashrate;
    if (Math.abs(signedDeviationRatio) < 0.0005) {
      return 'var(--p-card-background)';
    }

    const deviationRatio = Math.abs(signedDeviationRatio);
    const normalizedDeviation = Math.min(deviationRatio / 0.1, 1);
    const alpha = (0.18 + normalizedDeviation * 0.57).toFixed(2);
    return signedDeviationRatio > 0
      ? `rgba(34, 197, 94, ${alpha})`
      : `rgba(239, 68, 68, ${alpha})`;
  }
}
