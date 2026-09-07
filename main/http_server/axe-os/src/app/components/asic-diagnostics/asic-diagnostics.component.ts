import { CommonModule } from '@angular/common';
import { Component, OnDestroy, OnInit } from '@angular/core';
import { catchError, EMPTY, exhaustMap, Subject, takeUntil, timer } from 'rxjs';
import { AsicDiagnostics, AsicDiagnosticsChip } from '../../generated/models';
import { SystemApiService } from '../../services/system.service';

interface CoreView { id: number; pass: number; fail: number; ghs: number; }
interface DomainView { id: number; pass: number; fail: number; ghs: number; zero: number; cores: CoreView[]; }
interface ChipView { data: AsicDiagnosticsChip; domains: DomainView[]; ghs: number; }

@Component({
  selector: 'app-asic-diagnostics',
  standalone: true,
  imports: [CommonModule],
  templateUrl: './asic-diagnostics.component.html',
  styleUrls: ['./asic-diagnostics.component.scss']
})
export class AsicDiagnosticsComponent implements OnInit, OnDestroy {
  snapshot?: AsicDiagnostics;
  chips: ChipView[] = [];
  error = false;
  private destroy$ = new Subject<void>();

  constructor(private systemService: SystemApiService) {}

  ngOnInit(): void {
    timer(0, 5000).pipe(
      exhaustMap(() => this.systemService.getDiagnostics().pipe(catchError(() => {
        this.error = true;
        return EMPTY;
      }))),
      takeUntil(this.destroy$)
    ).subscribe(snapshot => this.setSnapshot(snapshot));
  }

  ngOnDestroy(): void { this.destroy$.next(); this.destroy$.complete(); }

  setSnapshot(snapshot: AsicDiagnostics): void {
    this.error = false;
    this.snapshot = snapshot;
    this.chips = snapshot.chips.map(data => {
      const valid = data.valid === 1 && data.windowSeconds > 0 &&
        data.corePassCounts.length === 156 && data.coreFailCounts.length === 156;
      const factor = valid ? 2 ** snapshot.leadingZeros / data.windowSeconds / 1e9 : 0;
      const domains: DomainView[] = valid ? Array.from({ length: 4 }, (_, id) => {
        const cores = data.corePassCounts.slice(id * 39, (id + 1) * 39).map((pass, i) => ({
          id: id * 39 + i, pass, fail: data.coreFailCounts[id * 39 + i], ghs: pass * factor
        }));
        const pass = cores.reduce((sum, core) => sum + core.pass, 0);
        return { id, cores, pass, fail: cores.reduce((sum, core) => sum + core.fail, 0),
          ghs: pass * factor, zero: cores.filter(core => core.pass === 0).length };
      }) : [];
      return { data, domains, ghs: domains.reduce((sum, domain) => sum + domain.ghs, 0) };
    });
  }

  get retained(): boolean {
    return this.error || this.snapshot?.mining !== 1 || this.snapshot?.source !== 'mining' ||
      this.chips.some(chip => chip.data.ageSeconds > 15);
  }

  trackChip(_index: number, chip: ChipView): number { return chip.data.chipId; }
  trackDomain(_index: number, domain: DomainView): number { return domain.id; }
  trackCore(_index: number, core: CoreView): number { return core.id; }
}
